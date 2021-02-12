#include "pch.h"
#include "Device.h"

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(Device, getDevice)

NTSTATUS Device::create(_In_ WDFDRIVER wdfDriver, _Inout_ PWDFDEVICE_INIT deviceInit)
{
    PAGED_CODE();

    WdfDeviceInitSetDeviceType(deviceInit, FILE_DEVICE_DISK);
    WdfDeviceInitSetIoType(deviceInit, WdfDeviceIoDirect);
    
    UNICODE_STRING deviceName;
    RtlInitUnicodeString(&deviceName, L"\\Device\\MyVirtualDisk");
    
    NTSTATUS status = STATUS_SUCCESS;
    status = WdfDeviceInitAssignName(deviceInit, &deviceName);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDFKEY keyImagePath;
    WdfDriverOpenParametersRegistryKey(wdfDriver, KEY_READ | KEY_WRITE, WDF_NO_OBJECT_ATTRIBUTES, &keyImagePath);
    WDFSTRING stringObjImagePath;
    status = WdfStringCreate(NULL, WDF_NO_OBJECT_ATTRIBUTES, &stringObjImagePath);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    UNICODE_STRING valueImagePath;
    RtlInitUnicodeString(&valueImagePath, L"ImagePath");
    status = WdfRegistryQueryString(keyImagePath, &valueImagePath, stringObjImagePath);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    UNICODE_STRING uniName;
    WdfStringGetUnicodeString(stringObjImagePath, &uniName);
        
    OBJECT_ATTRIBUTES objAttr;
    InitializeObjectAttributes(&objAttr, &uniName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    
    WDF_OBJECT_ATTRIBUTES fdoAttributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&fdoAttributes, Device);

    fdoAttributes.EvtCleanupCallback = onDeviceContextCleanup;

    HANDLE handle;
    IO_STATUS_BLOCK ioStatusBlock;
    status = ZwCreateFile(&handle,
        GENERIC_READ | GENERIC_WRITE,
        &objAttr, &ioStatusBlock,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        0,
        FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT,
        NULL, 0);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDFDEVICE hDevice;
    status = WdfDeviceCreate(&deviceInit, &fdoAttributes, &hDevice);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    Device* self = getDevice(hDevice);
    self->m_fileHandle = handle;

    FILE_STANDARD_INFORMATION fileInformation = { 0 };
    status = ZwQueryInformationFile(self->m_fileHandle, &ioStatusBlock, &fileInformation, sizeof(fileInformation), FileStandardInformation);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    self->m_fileSize = fileInformation.EndOfFile;

    WDFKEY keyDriveLetter;
    WdfDriverOpenParametersRegistryKey(wdfDriver, KEY_READ | KEY_WRITE, WDF_NO_OBJECT_ATTRIBUTES, &keyDriveLetter);
    WDFSTRING stringObjDriveLetter;
    status = WdfStringCreate(NULL, WDF_NO_OBJECT_ATTRIBUTES, &stringObjDriveLetter);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    UNICODE_STRING valueDriveLetter;
    RtlInitUnicodeString(&valueDriveLetter, L"DriveLetter");
    status = WdfRegistryQueryString(keyDriveLetter, &valueDriveLetter, stringObjDriveLetter);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    UNICODE_STRING symbolicLinkName;
    WdfStringGetUnicodeString(stringObjDriveLetter, &symbolicLinkName);

    status = WdfDeviceCreateSymbolicLink(hDevice, &symbolicLinkName);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = Device::init(hDevice, self);
    return status;
}

VOID Device::onDeviceContextCleanup(_In_ WDFOBJECT wdfDevice)
{
    UNREFERENCED_PARAMETER(wdfDevice);
    Device* self = getDevice((WDFDEVICE)wdfDevice);
    ZwClose(self->m_fileHandle);
    return;
}

NTSTATUS Device::init(WDFDEVICE hDevice, Device* self)
{
    NTSTATUS status = WdfDeviceCreateDeviceInterface(hDevice, (LPGUID)&GUID_DEVINTERFACE_VOLUME, NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoRead = onIoReadForward;
    queueConfig.EvtIoWrite = onIoWriteForward;
    queueConfig.EvtIoDeviceControl = onIoDeviceControl;
    
    WDFQUEUE queue;
    __analysis_assume(queueConfig.EvtIoStop != 0);
    status = WdfIoQueueCreate(hDevice, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);

    __analysis_assume(queueConfig.EvtIoStop == 0);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_IO_QUEUE_CONFIG newQueueConfig;
    WDF_IO_QUEUE_CONFIG_INIT(&newQueueConfig, WdfIoQueueDispatchParallel);

    newQueueConfig.EvtIoRead = onIoRead;
    newQueueConfig.EvtIoWrite = onIoWrite;

    WDF_OBJECT_ATTRIBUTES queueAttributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&queueAttributes);
    queueAttributes.ExecutionLevel = WdfExecutionLevelPassive;
    queueAttributes.ParentObject = hDevice;

    __analysis_assume(newQueueConfig.EvtIoStop != 0);
    WDFQUEUE newQueue;
    status = WdfIoQueueCreate(hDevice, &newQueueConfig, (WDF_OBJECT_ATTRIBUTES*)&queueAttributes, &newQueue);
    __analysis_assume(newQueueConfig.EvtIoStop == 0);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    self->m_fileQueue = newQueue;
    return status;
}

VOID Device::onIoRead(WDFQUEUE queue, WDFREQUEST request, size_t length)
{
    PAGED_CODE();
    PVOID Buffer;
    NTSTATUS status = WdfRequestRetrieveOutputBuffer(request, 0, &Buffer, NULL);
    ULONG_PTR bytesCopied = 0;
    if (NT_SUCCESS(status)) {
        WDF_REQUEST_PARAMETERS Param;
        WDF_REQUEST_PARAMETERS_INIT(&Param);
        WdfRequestGetParameters(request, &Param);

        auto self = getDevice(WdfIoQueueGetDevice(queue));
        IO_STATUS_BLOCK ioStatusBlock;
        status = ZwReadFile(self->m_fileHandle, NULL, NULL, NULL, &ioStatusBlock, Buffer, (ULONG)length, (PLARGE_INTEGER)&Param.Parameters.Write.DeviceOffset, NULL);
        bytesCopied = ioStatusBlock.Information;
    }
    WdfRequestCompleteWithInformation(request, status, bytesCopied);
}

VOID Device::onIoWrite(WDFQUEUE queue, WDFREQUEST request, size_t length)
{
    PAGED_CODE();

    PVOID Buffer;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(request, 0, &Buffer, NULL);
    
    ULONG_PTR bytesWritten = 0;
    if (NT_SUCCESS(status)) {
        WDF_REQUEST_PARAMETERS Param;
        WDF_REQUEST_PARAMETERS_INIT(&Param);
        WdfRequestGetParameters(request, &Param);
        
        auto self = getDevice(WdfIoQueueGetDevice(queue));
        IO_STATUS_BLOCK ioStatusBlock;
        status = ZwWriteFile(self->m_fileHandle, NULL, NULL, NULL, &ioStatusBlock, Buffer, (ULONG)length, (PLARGE_INTEGER)&Param.Parameters.Write.DeviceOffset, NULL);
        bytesWritten = ioStatusBlock.Information;
    }

    WdfRequestCompleteWithInformation(request, status, bytesWritten);
}

VOID Device::onIoReadForward(WDFQUEUE queue, WDFREQUEST request, size_t length)
{
    UNREFERENCED_PARAMETER(length);

    auto self = getDevice(WdfIoQueueGetDevice(queue));
    
    KIRQL oldIrql;
    KeRaiseIrql(DISPATCH_LEVEL, &oldIrql);
    WdfRequestForwardToIoQueue(request, self->m_fileQueue);
    KeLowerIrql(oldIrql);
}

VOID Device::onIoWriteForward(WDFQUEUE queue, WDFREQUEST request, size_t length)
{
    UNREFERENCED_PARAMETER(length);

    auto self = getDevice(WdfIoQueueGetDevice(queue));

    KIRQL oldIrql;
    KeRaiseIrql(DISPATCH_LEVEL, &oldIrql);
    WdfRequestForwardToIoQueue(request, self->m_fileQueue);
    KeLowerIrql(oldIrql);
}

VOID Device::onIoDeviceControl(_In_ WDFQUEUE queue, _In_ WDFREQUEST request, _In_ size_t outputBufferLength, _In_ size_t inputBufferLength, _In_ ULONG ioControlCode)
{
    UNREFERENCED_PARAMETER(inputBufferLength);

    PAGED_CODE();

    NTSTATUS status = STATUS_SUCCESS;
    ULONG_PTR bytesWritten = 0;

    KdPrint(("IoControlCode=0x%X\n", ioControlCode));

    switch (ioControlCode) {
    case IOCTL_STORAGE_GET_DEVICE_NUMBER:
    {
        STORAGE_DEVICE_NUMBER* storageDeviceNumber;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(STORAGE_DEVICE_NUMBER), (PVOID*)&storageDeviceNumber, NULL);
        if (!NT_SUCCESS(status)) {
            break;
        }

        storageDeviceNumber->DeviceType = FILE_DEVICE_DISK;
        storageDeviceNumber->DeviceNumber = 10;
        storageDeviceNumber->PartitionNumber = MAXULONG;

        status = STATUS_SUCCESS;
        bytesWritten = sizeof(STORAGE_DEVICE_NUMBER);
        break;
    }

    case IOCTL_STORAGE_GET_HOTPLUG_INFO:
    {
        STORAGE_HOTPLUG_INFO* storageHotplugInfo;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(STORAGE_HOTPLUG_INFO), (PVOID*)&storageHotplugInfo, NULL);
        if (!NT_SUCCESS(status)) {
            break;
        }

        storageHotplugInfo->Size = sizeof(STORAGE_HOTPLUG_INFO);
        storageHotplugInfo->MediaRemovable = FALSE;
        storageHotplugInfo->MediaHotplug = FALSE;
        storageHotplugInfo->DeviceHotplug = TRUE;
        storageHotplugInfo->WriteCacheEnableOverride = FALSE;

        status = STATUS_SUCCESS;
        bytesWritten = sizeof(STORAGE_HOTPLUG_INFO);
        break;
    }

    case IOCTL_STORAGE_QUERY_PROPERTY:
    {
        STORAGE_PROPERTY_QUERY* inputBuffer;
        status = WdfRequestRetrieveInputBuffer(request, sizeof(STORAGE_PROPERTY_QUERY), (PVOID*)&inputBuffer, NULL);
        if (!NT_SUCCESS(status)) {
            break;
        }

        ULONG_PTR bytesToCopy;
        switch (inputBuffer->PropertyId) {
        case StorageDeviceProperty:
        {
            STORAGE_DEVICE_DESCRIPTOR* outputBuffer;
            STORAGE_DEVICE_DESCRIPTOR sdd = { 1, sizeof sdd, 0, 0, TRUE, TRUE, 0, 0, 0, 0, BusTypeVirtual };
            status = WdfRequestRetrieveOutputBuffer(request, 0, (PVOID*)&outputBuffer, NULL);
            if (!NT_SUCCESS(status)) {
                break;
            }
            bytesToCopy = min(sizeof(sdd), outputBufferLength);
            status = STATUS_SUCCESS;
            memcpy(outputBuffer, &sdd, bytesToCopy);
            bytesWritten = bytesToCopy;
            break;
        }

        case StorageAdapterProperty:
        {
            STORAGE_ADAPTER_DESCRIPTOR sad = { 1, sizeof sad, PAGE_SIZE, 1, 0, TRUE, TRUE, TRUE, TRUE, BusTypeVirtual };
            STORAGE_ADAPTER_DESCRIPTOR* outputBuffer;
            status = WdfRequestRetrieveOutputBuffer(request, 0, (PVOID*)&outputBuffer, NULL);
            if (!NT_SUCCESS(status)) {
                break;
            }
            bytesToCopy = min(sizeof(sad), outputBufferLength);
            status = STATUS_SUCCESS;
            memcpy(outputBuffer, &sad, bytesToCopy);
            bytesWritten = bytesToCopy;
            break;
        }
        }
        break;
    }

    case IOCTL_DISK_GET_LENGTH_INFO:
    {
        GET_LENGTH_INFORMATION* getLengthInformation;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(GET_LENGTH_INFORMATION), (PVOID*)&getLengthInformation, NULL);
        if (!NT_SUCCESS(status)) {
            break;
        }

        auto self = getDevice(WdfIoQueueGetDevice(queue));
        getLengthInformation->Length = self->m_fileSize;
        bytesWritten = sizeof(*getLengthInformation);
        break;
    }

    case IOCTL_DISK_GET_MEDIA_TYPES:
    case IOCTL_STORAGE_GET_MEDIA_TYPES:
    case IOCTL_DISK_GET_DRIVE_GEOMETRY:
    {
        DISK_GEOMETRY* diskGeometry;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(DISK_GEOMETRY), (PVOID*)&diskGeometry, NULL);
        if (!NT_SUCCESS(status)) {
            break;
        }

        auto self = getDevice(WdfIoQueueGetDevice(queue));

        diskGeometry->BytesPerSector = 512;
        diskGeometry->SectorsPerTrack = 1;
        diskGeometry->TracksPerCylinder = 1;
        diskGeometry->Cylinders.QuadPart = self->m_fileSize.QuadPart / diskGeometry->BytesPerSector;
        diskGeometry->MediaType = RemovableMedia; //FixedMedia;

        if (diskGeometry->Cylinders.QuadPart * diskGeometry->BytesPerSector < self->m_fileSize.QuadPart)
        {
            ++diskGeometry->Cylinders.QuadPart;
        }

        bytesWritten = sizeof(*diskGeometry);
        break;
    }

    case IOCTL_SCSI_GET_ADDRESS:
    {
        SCSI_ADDRESS* scsiAddress;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(SCSI_ADDRESS), (PVOID*)&scsiAddress, NULL);
        if (!NT_SUCCESS(status)) {
            break;
        }

        scsiAddress->Length = sizeof(SCSI_ADDRESS);
        scsiAddress->Lun = 0;
        scsiAddress->PathId = 0;
        scsiAddress->PortNumber = 0;
        scsiAddress->TargetId = 0;

        bytesWritten = sizeof(*scsiAddress);
        break;
    }

    case IOCTL_DISK_ARE_VOLUMES_READY:
    {
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_DISK_VOLUMES_ARE_READY:
    {
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_MOUNTDEV_QUERY_DEVICE_NAME:
    {
        MOUNTDEV_NAME* mountDevName;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(MOUNTDEV_NAME), (PVOID*)&mountDevName, NULL);
        if (!NT_SUCCESS(status)) {
            break;
        }

        wcscpy(mountDevName->Name, L"\\DosDevices\\W:");
        mountDevName->NameLength = sizeof(mountDevName->Name);
        status = STATUS_SUCCESS;
        bytesWritten = sizeof(*mountDevName);

        break;
    }

    case IOCTL_STORAGE_MANAGE_DATA_SET_ATTRIBUTES:
    {
        status = STATUS_SUCCESS;
        break;
    }
    case IOCTL_DISK_IS_WRITABLE:
    {
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_DISK_GET_PARTITION_INFO:
    {
        PARTITION_INFORMATION* partInfo;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(PARTITION_INFORMATION), (PVOID*)&partInfo, NULL);
        if (!NT_SUCCESS(status)) {
            break;
        }

        auto self = getDevice(WdfIoQueueGetDevice(queue));

        partInfo->StartingOffset.QuadPart = 512;
        partInfo->PartitionLength.QuadPart = self->m_fileSize.QuadPart - 512;
        partInfo->HiddenSectors = 1;
        partInfo->PartitionNumber = 0;
        partInfo->PartitionType = PARTITION_ENTRY_UNUSED;
        partInfo->BootIndicator = FALSE;
        partInfo->RecognizedPartition = FALSE;
        partInfo->RewritePartition = FALSE;

        bytesWritten = sizeof(*partInfo);
    }

    case IOCTL_DISK_SET_PARTITION_INFO:
    {
        if (inputBufferLength < sizeof(SET_PARTITION_INFORMATION))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }
        status = STATUS_SUCCESS;
        break;
    }

    default:
        status = STATUS_NOT_SUPPORTED;
    }

    WdfRequestCompleteWithInformation(request, status, bytesWritten);
}
