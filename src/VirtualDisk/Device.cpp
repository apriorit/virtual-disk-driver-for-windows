#include "pch.h"
#include "Device.h"

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(Device, DeviceGetData)

NTSTATUS Device::create(_Inout_ PWDFDEVICE_INIT deviceInit)
{
    NTSTATUS                  status = STATUS_SUCCESS;
    Device*                   deviceData; 
    WDF_OBJECT_ATTRIBUTES     fdoAttributes;
    WDFDEVICE                 hDevice;
    UNICODE_STRING            uniName;
    OBJECT_ATTRIBUTES         objAttr;
    HANDLE                    handle;
    IO_STATUS_BLOCK           ioStatusBlock;
    FILE_STANDARD_INFORMATION fileInformation = { 0 };
    UNICODE_STRING            deviceName;
    UNICODE_STRING            symbolicLinkName;

    PAGED_CODE();

    WdfDeviceInitSetDeviceType(deviceInit, FILE_DEVICE_DISK);
    WdfDeviceInitSetIoType(deviceInit, WdfDeviceIoDirect);

    RtlInitUnicodeString(&deviceName, L"\\Device\\MyVirtualDisk");
    status = WdfDeviceInitAssignName(deviceInit, &deviceName);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    RtlInitUnicodeString(&uniName, L"\\DosDevices\\C:\\example.txt");

    InitializeObjectAttributes(&objAttr, &uniName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&fdoAttributes, Device);


    fdoAttributes.EvtCleanupCallback = evtDeviceContextCleanup;

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

    status = WdfDeviceCreate(&deviceInit, &fdoAttributes, &hDevice);
    if (!NT_SUCCESS(status)) {
        return status;
    }


    deviceData = DeviceGetData(hDevice);
    deviceData->handle = handle;
    status = ZwQueryInformationFile(deviceData->handle, &ioStatusBlock, &fileInformation, sizeof(fileInformation), FileStandardInformation);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    deviceData->fileSize = fileInformation.EndOfFile;

    RtlInitUnicodeString(&symbolicLinkName, L"\\DosDevices\\W:");
    status = WdfDeviceCreateSymbolicLink(hDevice, &symbolicLinkName);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = Device::init(hDevice, deviceData);

    return status;
}

VOID Device::evtDeviceContextCleanup(_In_  WDFOBJECT wdfDevice)
{
    Device* deviceData;
    UNREFERENCED_PARAMETER(wdfDevice);

    deviceData = DeviceGetData((WDFDEVICE)wdfDevice);

    ZwClose(deviceData->handle);
    return;
}

NTSTATUS Device::init(WDFDEVICE hDevice, Device* deviceData)
{
    WDF_IO_QUEUE_CONFIG       queueConfig;
    WDF_IO_QUEUE_CONFIG       newQueueConfig;
    WDFQUEUE                  newQueue;
    WDF_OBJECT_ATTRIBUTES     queueAttributes;
    WDFQUEUE                  queue;

    NTSTATUS status = WdfDeviceCreateDeviceInterface(
        hDevice,
        (LPGUID)&GUID_DEVINTERFACE_VOLUME,
        NULL 
    );

    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);

    queueConfig.EvtIoRead = evtIoReadForward;
    queueConfig.EvtIoWrite = evtIoWriteForward;
    queueConfig.EvtIoDeviceControl = evtIoDeviceControl;

    __analysis_assume(queueConfig.EvtIoStop != 0);
    status = WdfIoQueueCreate(
        hDevice,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &queue
    );
    __analysis_assume(queueConfig.EvtIoStop == 0);

    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT(&newQueueConfig, WdfIoQueueDispatchParallel);

    newQueueConfig.EvtIoRead = evtIoRead;
    newQueueConfig.EvtIoWrite = evtIoWrite;

    WDF_OBJECT_ATTRIBUTES_INIT(&queueAttributes);
    queueAttributes.ExecutionLevel = WdfExecutionLevelPassive;
    queueAttributes.ParentObject = hDevice;

    __analysis_assume(newQueueConfig.EvtIoStop != 0);
    status = WdfIoQueueCreate(
        hDevice,
        &newQueueConfig,
        (WDF_OBJECT_ATTRIBUTES*)&queueAttributes,
        &newQueue
    );
    __analysis_assume(newQueueConfig.EvtIoStop == 0);

    if (!NT_SUCCESS(status)) {

        return status;
    }

    deviceData->customQueue = newQueue;

    return status;
}

VOID Device::evtIoRead(WDFQUEUE queue, WDFREQUEST request, size_t length)
{
    NTSTATUS                 status;
    ULONG_PTR                bytesCopied = 0;
    PVOID                    Buffer;
    WDF_REQUEST_PARAMETERS   Param;
    Device*                  deviceData;
    IO_STATUS_BLOCK          ioStatusBlock;
    WDFDEVICE                hDevice;

    PAGED_CODE();


    status = WdfRequestRetrieveOutputBuffer(request, 0, &Buffer, NULL);
    if (NT_SUCCESS(status)) {
        WDF_REQUEST_PARAMETERS_INIT(&Param);
        WdfRequestGetParameters(request, &Param);

        hDevice = WdfIoQueueGetDevice(queue);

        deviceData = DeviceGetData(hDevice);
        status = ZwReadFile(deviceData->handle, NULL, NULL, NULL, &ioStatusBlock, Buffer, (ULONG)length, (PLARGE_INTEGER)&Param.Parameters.Write.DeviceOffset, NULL);


        bytesCopied = ioStatusBlock.Information;
    }

    WdfRequestCompleteWithInformation(request, status, bytesCopied);
}

VOID Device::evtIoWrite(WDFQUEUE queue, WDFREQUEST request, size_t length)
{
    NTSTATUS                 status;
    ULONG_PTR                bytesWritten = 0;
    PVOID                    Buffer;
    WDF_REQUEST_PARAMETERS   Param;
    Device*                  deviceData;
    IO_STATUS_BLOCK          ioStatusBlock;
    WDFDEVICE                hDevice;
    
    PAGED_CODE();

    status = WdfRequestRetrieveInputBuffer(request, 0, &Buffer, NULL);

    if (NT_SUCCESS(status)) {
        WDF_REQUEST_PARAMETERS_INIT(&Param);
        WdfRequestGetParameters(request, &Param);

        hDevice = WdfIoQueueGetDevice(queue);

        deviceData = DeviceGetData(hDevice);
        status = ZwWriteFile(deviceData->handle, NULL, NULL, NULL, &ioStatusBlock, Buffer, (ULONG)length, (PLARGE_INTEGER)&Param.Parameters.Write.DeviceOffset, NULL);
        bytesWritten = ioStatusBlock.Information;
    }

    WdfRequestCompleteWithInformation(request, status, bytesWritten);
}

VOID Device::evtIoReadForward(WDFQUEUE queue, WDFREQUEST request, size_t length)
{
    WDFDEVICE    hDevice;
    Device*     deviceData;
    KIRQL        oldIrql;

    UNREFERENCED_PARAMETER(length);

    KdPrint(("ToasterEvtIoReadForward called;\n"));

    hDevice = WdfIoQueueGetDevice(queue);
    deviceData = DeviceGetData(hDevice);

    KeRaiseIrql(DISPATCH_LEVEL, &oldIrql);
    WdfRequestForwardToIoQueue(request, deviceData->customQueue);
    KeLowerIrql(oldIrql);
}

VOID Device::evtIoWriteForward(WDFQUEUE queue, WDFREQUEST request, size_t length)
{
    WDFDEVICE    hDevice;
    Device*      deviceData;
    KIRQL        oldIrql;

    UNREFERENCED_PARAMETER(length);

    KdPrint(("ToasterEvtIoWriteForward called;\n"));

    hDevice = WdfIoQueueGetDevice(queue);
    deviceData = DeviceGetData(hDevice);

    KeRaiseIrql(DISPATCH_LEVEL, &oldIrql);
    WdfRequestForwardToIoQueue(request, deviceData->customQueue);
    KeLowerIrql(oldIrql);
}

VOID Device::evtIoDeviceControl(_In_ WDFQUEUE queue, _In_ WDFREQUEST request, _In_ size_t outputBufferLength, _In_ size_t inputBufferLength, _In_ ULONG ioControlCode)
{
    NTSTATUS  status = STATUS_SUCCESS;
    ULONG_PTR  bytesWritten = 0;

    UNREFERENCED_PARAMETER(queue);
    UNREFERENCED_PARAMETER(outputBufferLength);
    UNREFERENCED_PARAMETER(inputBufferLength);

    PAGED_CODE();

    switch (ioControlCode) {
    case IOCTL_STORAGE_GET_DEVICE_NUMBER:
    {
        STORAGE_DEVICE_NUMBER* storageDeviceNumber;

        status = WdfRequestRetrieveOutputBuffer(request, sizeof(STORAGE_DEVICE_NUMBER), (PVOID*)&storageDeviceNumber, NULL);
        if (!NT_SUCCESS(status)) {
            KdPrint(("WdfRequestRetrieveOutputBuffer failed 0x%x\n", status));
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
        ULONG_PTR  bytesToCopy;

        status = WdfRequestRetrieveInputBuffer(request, sizeof(STORAGE_PROPERTY_QUERY), (PVOID*)&inputBuffer, NULL);
        if (!NT_SUCCESS(status)) {
            KdPrint(("WdfRequestRetrieveOutputBuffer failed 0x%x\n", status));
            break;
        }

        switch (inputBuffer->PropertyId) {
        case StorageDeviceProperty:
        {
            STORAGE_DEVICE_DESCRIPTOR* outputBuffer;
            STORAGE_DEVICE_DESCRIPTOR sdd = { 1, sizeof sdd, 0, 0, TRUE, TRUE, 0, 0, 0, 0, BusTypeVirtual };
            status = WdfRequestRetrieveOutputBuffer(request, 0, (PVOID*)&outputBuffer, NULL);
            if (!NT_SUCCESS(status)) {
                KdPrint(("WdfRequestRetrieveOutputBuffer failed 0x%x\n", status));
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
            STORAGE_ADAPTER_DESCRIPTOR* outputBuffer;
            STORAGE_ADAPTER_DESCRIPTOR sad = { 1, sizeof sad, PAGE_SIZE, 1, 0, TRUE, TRUE, TRUE, TRUE, BusTypeVirtual };
            status = WdfRequestRetrieveOutputBuffer(request, 0, (PVOID*)&outputBuffer, NULL);
            if (!NT_SUCCESS(status)) {
                KdPrint(("WdfRequestRetrieveOutputBuffer failed 0x%x\n", status));
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
        Device*                   deviceData;
        WDFDEVICE                 hDevice;
        GET_LENGTH_INFORMATION*   getLengthInformation;
        
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(GET_LENGTH_INFORMATION), (PVOID*)&getLengthInformation, NULL);
        if (!NT_SUCCESS(status)) {
            KdPrint(("WdfRequestRetrieveOutputBuffer failed 0x%x\n", status));
            break;
        }

        hDevice = WdfIoQueueGetDevice(queue);
        deviceData = DeviceGetData(hDevice);
        getLengthInformation->Length = deviceData->fileSize;
        bytesWritten = sizeof(*getLengthInformation);

        break;
    }

    case IOCTL_DISK_GET_DRIVE_GEOMETRY:
    {
        Device*                   deviceData;
        WDFDEVICE                 hDevice;
        DISK_GEOMETRY* diskGeometry;

        status = WdfRequestRetrieveOutputBuffer(request, sizeof(DISK_GEOMETRY), (PVOID*)&diskGeometry, NULL);
        if (!NT_SUCCESS(status)) {
            KdPrint(("WdfRequestRetrieveOutputBuffer failed 0x%x\n", status));
            break;
        }

        hDevice = WdfIoQueueGetDevice(queue);
        deviceData = DeviceGetData(hDevice);

        diskGeometry->BytesPerSector = 512;
        diskGeometry->SectorsPerTrack = 1;
        diskGeometry->TracksPerCylinder = 1;
        diskGeometry->Cylinders.QuadPart = deviceData->fileSize.QuadPart / diskGeometry->BytesPerSector;
        diskGeometry->MediaType = FixedMedia;

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


    default:
        status = STATUS_NOT_SUPPORTED;
    }


    WdfRequestCompleteWithInformation(request, status, bytesWritten);
}