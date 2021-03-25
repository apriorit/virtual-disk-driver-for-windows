#include "pch.h"
#include "Device.h"
#include "DevPropKeys.h"

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(Device, getDevice)
const int gDeviceNumber = 10;
const unsigned int kMaxDeviceNameLen = 100;

NTSTATUS Device::create(_In_ WDFDRIVER wdfDriver, _Inout_ PWDFDEVICE_INIT deviceInit)
{
    UNREFERENCED_PARAMETER(wdfDriver);

    WdfDeviceInitSetDeviceType(deviceInit, FILE_DEVICE_DISK);
    WdfDeviceInitSetIoType(deviceInit, WdfDeviceIoDirect);

    static int counter{1};
    WDFMEMORY memUniqueName;
    PVOID bufferUniqueName;
    NTSTATUS status = WdfMemoryCreate(WDF_NO_OBJECT_ATTRIBUTES, PagedPool, 0, sizeof(wchar_t) * kMaxDeviceNameLen, &memUniqueName, &bufferUniqueName);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    UNICODE_STRING uniqueDeviceName;
    uniqueDeviceName.Buffer = reinterpret_cast<PWCH>(bufferUniqueName);
    uniqueDeviceName.Length = static_cast <USHORT>(sizeof(wchar_t) * kMaxDeviceNameLen - sizeof(wchar_t));
    uniqueDeviceName.MaximumLength = static_cast <USHORT>(sizeof(wchar_t) * kMaxDeviceNameLen);
    RtlUnicodeStringPrintf(&uniqueDeviceName, L"\\Device\\MyVirtualDisk-%d", counter);
    ++counter;

    status = WdfDeviceInitAssignName(deviceInit, &uniqueDeviceName);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = WdfDeviceInitAssignSDDLString(deviceInit, &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RWX_RES_RWX);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_DEVICE_PROPERTY_DATA devPropData{};
    devPropData.PropertyKey = &DEVPKEY_VIRTUALDISK_FILEPATH;
    devPropData.Lcid = LOCALE_NEUTRAL;
    devPropData.Size = sizeof(WDF_DEVICE_PROPERTY_DATA);

    WDFMEMORY memFilePath{};
    DEVPROPTYPE devPropType;
    status = WdfFdoInitAllocAndQueryPropertyEx(deviceInit, &devPropData, PagedPool, WDF_NO_OBJECT_ATTRIBUTES, &memFilePath, &devPropType );
    if (!NT_SUCCESS(status))
    {
        return status;
    }
    size_t bufSize{};
    PVOID bufFilePath = WdfMemoryGetBuffer(memFilePath, &bufSize);

    UNICODE_STRING uniFilePath;
    uniFilePath.Buffer = reinterpret_cast<PWCH>(bufFilePath);
    uniFilePath.Length = static_cast<USHORT>(bufSize-sizeof(wchar_t));
    uniFilePath.MaximumLength = static_cast<USHORT>(bufSize);
 
    OBJECT_ATTRIBUTES objAttr;
    InitializeObjectAttributes(&objAttr, &uniFilePath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);

    WDF_OBJECT_ATTRIBUTES fdoAttributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&fdoAttributes, Device);

    fdoAttributes.EvtCleanupCallback = onDeviceContextCleanup;

    HANDLE handle;
    IO_STATUS_BLOCK ioStatusBlock;
    status = ZwCreateFile(&handle,
        GENERIC_READ | GENERIC_WRITE,
        &objAttr, &ioStatusBlock,
        nullptr,
        FILE_ATTRIBUTE_NORMAL,
        0,
        FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT,
        nullptr, 0);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    WDFDEVICE hDevice;
    status = WdfDeviceCreate(&deviceInit, &fdoAttributes, &hDevice);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    Device* self = getDevice(hDevice);
    self->m_fileHandle = handle;

    FILE_STANDARD_INFORMATION fileInformation = {};
    status = ZwQueryInformationFile(self->m_fileHandle, &ioStatusBlock, &fileInformation, sizeof(fileInformation), FileStandardInformation);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    self->m_fileSize = fileInformation.EndOfFile;

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
    NTSTATUS status = WdfDeviceCreateDeviceInterface(hDevice, (LPGUID)&GUID_DEVINTERFACE_VOLUME, nullptr);
    if (!NT_SUCCESS(status))
    {
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
    if (!NT_SUCCESS(status))
    {
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
    if (!NT_SUCCESS(status))
    {
        return status;
    }
    self->m_fileQueue = newQueue;
    return status;
}

VOID Device::onIoRead(WDFQUEUE queue, WDFREQUEST request, size_t length)
{
    PVOID outputBuffer;
    NTSTATUS status = WdfRequestRetrieveOutputBuffer(request, 0, &outputBuffer, nullptr);
    ULONG_PTR bytesCopied = 0;
    if (NT_SUCCESS(status))
    {
        WDF_REQUEST_PARAMETERS requestParams;
        WDF_REQUEST_PARAMETERS_INIT(&requestParams);
        WdfRequestGetParameters(request, &requestParams);

        auto self = getDevice(WdfIoQueueGetDevice(queue));
        IO_STATUS_BLOCK ioStatusBlock;
        status = ZwReadFile(self->m_fileHandle, nullptr, nullptr, nullptr, &ioStatusBlock, outputBuffer, (ULONG)length, (PLARGE_INTEGER)&requestParams.Parameters.Write.DeviceOffset, nullptr);
        bytesCopied = ioStatusBlock.Information;
    }
    WdfRequestCompleteWithInformation(request, status, bytesCopied);
}

VOID Device::onIoWrite(WDFQUEUE queue, WDFREQUEST request, size_t length)
{
    PVOID inputBuffer;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(request, 0, &inputBuffer, nullptr);

    ULONG_PTR bytesWritten = 0;
    if (NT_SUCCESS(status))
    {
        WDF_REQUEST_PARAMETERS requestParams;
        WDF_REQUEST_PARAMETERS_INIT(&requestParams);
        WdfRequestGetParameters(request, &requestParams);

        auto self = getDevice(WdfIoQueueGetDevice(queue));
        IO_STATUS_BLOCK ioStatusBlock;
        status = ZwWriteFile(self->m_fileHandle, nullptr, nullptr, nullptr, &ioStatusBlock, inputBuffer, (ULONG)length, (PLARGE_INTEGER)&requestParams.Parameters.Write.DeviceOffset, nullptr);
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

    NTSTATUS status = STATUS_SUCCESS;
    ULONG_PTR bytesWritten = 0;

    KdPrint(("IoControlCode=0x%X\n", ioControlCode));

    switch (ioControlCode) {
    case IOCTL_STORAGE_GET_DEVICE_NUMBER:
    {
        STORAGE_DEVICE_NUMBER* storageDeviceNumber;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(STORAGE_DEVICE_NUMBER), (PVOID*)&storageDeviceNumber, nullptr);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        storageDeviceNumber->DeviceType = FILE_DEVICE_DISK;
        storageDeviceNumber->DeviceNumber = gDeviceNumber;
        storageDeviceNumber->PartitionNumber = MAXULONG;

        status = STATUS_SUCCESS;
        bytesWritten = sizeof(STORAGE_DEVICE_NUMBER);
        break;
    }

    case IOCTL_STORAGE_GET_HOTPLUG_INFO:
    {
        STORAGE_HOTPLUG_INFO* storageHotplugInfo;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(STORAGE_HOTPLUG_INFO), (PVOID*)&storageHotplugInfo, nullptr);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        storageHotplugInfo->Size = sizeof(STORAGE_HOTPLUG_INFO);
        storageHotplugInfo->MediaRemovable = false;
        storageHotplugInfo->MediaHotplug = false;
        storageHotplugInfo->DeviceHotplug = true;
        storageHotplugInfo->WriteCacheEnableOverride = false;

        status = STATUS_SUCCESS;
        bytesWritten = sizeof(STORAGE_HOTPLUG_INFO);
        break;
    }

    case IOCTL_STORAGE_QUERY_PROPERTY:
    {
        STORAGE_PROPERTY_QUERY* inputBuffer;
        status = WdfRequestRetrieveInputBuffer(request, sizeof(STORAGE_PROPERTY_QUERY), (PVOID*)&inputBuffer, nullptr);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        ULONG_PTR bytesToCopy;
        switch (inputBuffer->PropertyId)
        {
        case StorageDeviceProperty:
        {
            STORAGE_DEVICE_DESCRIPTOR* outputBuffer;
            STORAGE_DEVICE_DESCRIPTOR sdd = { 1, sizeof sdd, 0, 0, true, true, 0, 0, 0, 0, BusTypeVirtual };
            status = WdfRequestRetrieveOutputBuffer(request, 0, (PVOID*)&outputBuffer, nullptr);
            if (!NT_SUCCESS(status))
            {
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
            STORAGE_ADAPTER_DESCRIPTOR sad = { 1, sizeof sad, PAGE_SIZE, 1, 0, true, true, true, true, BusTypeVirtual };
            STORAGE_ADAPTER_DESCRIPTOR* outputBuffer;
            status = WdfRequestRetrieveOutputBuffer(request, 0, (PVOID*)&outputBuffer, nullptr);
            if (!NT_SUCCESS(status))
            {
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
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(GET_LENGTH_INFORMATION), (PVOID*)&getLengthInformation, nullptr);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        auto self = getDevice(WdfIoQueueGetDevice(queue));
        getLengthInformation->Length = self->m_fileSize;
        bytesWritten = sizeof(*getLengthInformation);
        break;
    }

    case IOCTL_DISK_GET_MEDIA_TYPES:
    case IOCTL_DISK_GET_DRIVE_GEOMETRY:
    {
        DISK_GEOMETRY* diskGeometry;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(DISK_GEOMETRY), (PVOID*)&diskGeometry, nullptr);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        auto self = getDevice(WdfIoQueueGetDevice(queue));

        diskGeometry->BytesPerSector = 512;
        diskGeometry->SectorsPerTrack = 1;
        diskGeometry->TracksPerCylinder = 1;
        diskGeometry->Cylinders.QuadPart = (self->m_fileSize.QuadPart + diskGeometry->BytesPerSector - 1) / diskGeometry->BytesPerSector;
        diskGeometry->MediaType = RemovableMedia;

        bytesWritten = sizeof(*diskGeometry);
        break;
    }

    case IOCTL_STORAGE_MANAGE_DATA_SET_ATTRIBUTES:
    case IOCTL_DISK_IS_WRITABLE:
    {
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_MOUNTDEV_QUERY_DEVICE_NAME:
    {
        MOUNTDEV_NAME* mountDevName;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(*mountDevName), (PVOID*)&mountDevName, nullptr);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        WDFSTRING deviceName;
        status = WdfStringCreate(nullptr, WDF_NO_OBJECT_ATTRIBUTES, &deviceName);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        status = WdfDeviceRetrieveDeviceName(WdfIoQueueGetDevice(queue), deviceName);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        UNICODE_STRING uniDeviceName;
        WdfStringGetUnicodeString(deviceName, &uniDeviceName);

        mountDevName->NameLength = uniDeviceName.Length;

        if (FIELD_OFFSET(MOUNTDEV_NAME, Name) + uniDeviceName.Length > outputBufferLength)
        {
            status = STATUS_BUFFER_OVERFLOW;
            bytesWritten = sizeof(*mountDevName);
        }
        else
        {
            memcpy(mountDevName->Name, uniDeviceName.Buffer, uniDeviceName.Length);
            status = STATUS_SUCCESS;
            bytesWritten = FIELD_OFFSET(MOUNTDEV_NAME, Name) + uniDeviceName.Length;
        }
        break;
    }

    case IOCTL_MOUNTDEV_QUERY_UNIQUE_ID:
    {
        MOUNTDEV_UNIQUE_ID* mountDevUniqueId;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(*mountDevUniqueId), (PVOID*)&mountDevUniqueId, nullptr);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        WDF_DEVICE_PROPERTY_DATA devPropData{};
        devPropData.PropertyKey = &DEVPKEY_Device_InstanceId;
        devPropData.Lcid = LOCALE_NEUTRAL;
        devPropData.Size = sizeof(WDF_DEVICE_PROPERTY_DATA);

        WDFMEMORY deviceInstanceId{};
        DEVPROPTYPE devPropType;
        status = WdfDeviceAllocAndQueryPropertyEx(WdfIoQueueGetDevice(queue), &devPropData, PagedPool, WDF_NO_OBJECT_ATTRIBUTES, &deviceInstanceId, &devPropType);
        if (!NT_SUCCESS(status))
        {
            break;
        }
        size_t bufSize{};
        PVOID bufInstanceId = WdfMemoryGetBuffer(deviceInstanceId, &bufSize);

        UNICODE_STRING uniInstanceId;
        uniInstanceId.Buffer = reinterpret_cast<PWCH>(bufInstanceId);
        uniInstanceId.Length = static_cast<USHORT>(bufSize - sizeof(wchar_t));
        uniInstanceId.MaximumLength = static_cast<USHORT>(bufSize);

        mountDevUniqueId->UniqueIdLength = uniInstanceId.Length;

        if (FIELD_OFFSET(MOUNTDEV_UNIQUE_ID, UniqueId) + uniInstanceId.Length > outputBufferLength)
        {
            status = STATUS_BUFFER_OVERFLOW;
            bytesWritten = sizeof(*mountDevUniqueId);
        }
        else
        {
            memcpy(mountDevUniqueId->UniqueId, uniInstanceId.Buffer, uniInstanceId.Length);
            status = STATUS_SUCCESS;
            bytesWritten = FIELD_OFFSET(MOUNTDEV_UNIQUE_ID, UniqueId) + uniInstanceId.Length;
        }
        break;
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
        status = STATUS_INVALID_DEVICE_REQUEST;
    }

    WdfRequestCompleteWithInformation(request, status, bytesWritten);
}
