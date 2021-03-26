#include "pch.h"
#include "Device.h"
#include "DevPropKeys.h"

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(Device, getDevice)
long Device::m_counter = 1;

inline void* __cdecl operator new(size_t, void* ptr)
{
    return ptr;
}

void __cdecl operator delete(void*, size_t)
{
}

NTSTATUS Device::create(_In_ WDFDRIVER wdfDriver, _Inout_ PWDFDEVICE_INIT deviceInit)
{
    WdfDeviceInitSetDeviceType(deviceInit, FILE_DEVICE_DISK);
    WdfDeviceInitSetIoType(deviceInit, WdfDeviceIoDirect);

    const int kMaxDeviceNameLen = sizeof(L"\\DeviceMyVirtualDisk-00000000");
    WDFMEMORY memUniqueName;
    PVOID bufferUniqueName;
    WDF_OBJECT_ATTRIBUTES  attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = getDevice(wdfDriver);
    NTSTATUS status = WdfMemoryCreate(&attributes, PagedPool, 0, sizeof(wchar_t) * kMaxDeviceNameLen, &memUniqueName, &bufferUniqueName);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    UNICODE_STRING uniqueDeviceName;
    uniqueDeviceName.Buffer = reinterpret_cast<PWCH>(bufferUniqueName);
    uniqueDeviceName.Length = 0;
    uniqueDeviceName.MaximumLength = static_cast <USHORT>(sizeof(wchar_t) * kMaxDeviceNameLen);
    RtlUnicodeStringPrintf(&uniqueDeviceName, L"\\Device\\MyVirtualDisk-%d", m_counter);
    InterlockedIncrement(&m_counter);

    status = WdfDeviceInitAssignName(deviceInit, &uniqueDeviceName);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = WdfDeviceInitAssignSDDLString(deviceInit, &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RWX_RES_RWX);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_DEVICE_PROPERTY_DATA devPropData{};
    WDF_DEVICE_PROPERTY_DATA_INIT(&devPropData, &DEVPKEY_VIRTUALDISK_FILEPATH);
   
    WDFMEMORY memFilePath{};
    DEVPROPTYPE devPropType;
    status = WdfFdoInitAllocAndQueryPropertyEx(deviceInit, &devPropData, PagedPool, &attributes, &memFilePath, &devPropType );
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
 
    OBJECT_ATTRIBUTES objAttr = RTL_INIT_OBJECT_ATTRIBUTES(&uniFilePath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE);

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
    status = self->init(hDevice, ioStatusBlock);
    return status;
}

VOID Device::onDeviceContextCleanup(_In_ WDFOBJECT wdfDevice)
{
    getDevice((WDFDEVICE)wdfDevice)->~Device();
    return;
}

Device::~Device()
{
    ZwClose(this->m_fileHandle);
}

NTSTATUS Device::init(WDFDEVICE hDevice, IO_STATUS_BLOCK& ioStatusBlock)
{
    FILE_STANDARD_INFORMATION fileInformation = {};
    NTSTATUS status = ZwQueryInformationFile(this->m_fileHandle, &ioStatusBlock, &fileInformation, sizeof(fileInformation), FileStandardInformation);
    if (!NT_SUCCESS(status))
    {
        return status;
    }
    this->m_fileSize = fileInformation.EndOfFile;

    status = WdfDeviceCreateDeviceInterface(hDevice, (LPGUID)&GUID_DEVINTERFACE_VOLUME, nullptr);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoRead = onIoReadWriteForward;
    queueConfig.EvtIoWrite = onIoReadWriteForward;
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
    status = WdfIoQueueCreate(hDevice, &newQueueConfig, &queueAttributes, &newQueue);
    __analysis_assume(newQueueConfig.EvtIoStop == 0);
    if (!NT_SUCCESS(status))
    {
        return status;
    }
    this->m_fileQueue = newQueue;
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

VOID Device::onIoReadWriteForward(WDFQUEUE queue, WDFREQUEST request, size_t)
{
    auto self = getDevice(WdfIoQueueGetDevice(queue));

    KIRQL oldIrql;
    KeRaiseIrql(DISPATCH_LEVEL, &oldIrql);
    WdfRequestForwardToIoQueue(request, self->m_fileQueue);
    KeLowerIrql(oldIrql);
}

VOID Device::onIoDeviceControl(_In_ WDFQUEUE queue, _In_ WDFREQUEST request, _In_ size_t outputBufferLength, _In_ size_t, _In_ ULONG ioControlCode)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG_PTR bytesWritten = 0;

    KdPrint(("IoControlCode=0x%X\n", ioControlCode));

    switch (ioControlCode) {
    case IOCTL_STORAGE_GET_DEVICE_NUMBER:
    {
        STORAGE_DEVICE_NUMBER* info;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(*info), reinterpret_cast<PVOID*>(&info), nullptr);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        info->DeviceType = FILE_DEVICE_DISK;
        info->DeviceNumber = MAXULONG;
        info->PartitionNumber = MAXULONG;

        bytesWritten = sizeof(*info);
        break;
    }

    case IOCTL_STORAGE_GET_HOTPLUG_INFO:
    {
        STORAGE_HOTPLUG_INFO* info;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(*info), reinterpret_cast<PVOID*>(&info), nullptr);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        info->Size = sizeof(STORAGE_HOTPLUG_INFO);
        info->MediaRemovable = false;
        info->MediaHotplug = false;
        info->DeviceHotplug = true;
        info->WriteCacheEnableOverride = false;

        bytesWritten = sizeof(*info);
        break;
    }

    case IOCTL_DISK_GET_LENGTH_INFO:
    {
        GET_LENGTH_INFORMATION* info;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(*info), reinterpret_cast<PVOID*>(&info), nullptr);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        auto self = getDevice(WdfIoQueueGetDevice(queue));
        info->Length = self->m_fileSize;
        bytesWritten = sizeof(*info);
        break;
    }

    case IOCTL_DISK_GET_MEDIA_TYPES:
    case IOCTL_DISK_GET_DRIVE_GEOMETRY:
    {
        DISK_GEOMETRY* info;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(*info), reinterpret_cast<PVOID*>(&info), nullptr);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        auto self = getDevice(WdfIoQueueGetDevice(queue));

        info->BytesPerSector = 512;
        info->SectorsPerTrack = 1;
        info->TracksPerCylinder = 1;
        info->Cylinders.QuadPart = (self->m_fileSize.QuadPart + info->BytesPerSector - 1) / info->BytesPerSector;
        info->MediaType = RemovableMedia;

        bytesWritten = sizeof(*info);
        break;
    }

    case IOCTL_DISK_IS_WRITABLE:
    {
        break;
    }

    case IOCTL_MOUNTDEV_QUERY_DEVICE_NAME:
    {
        MOUNTDEV_NAME* info;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(*info), reinterpret_cast<PVOID*>(&info), nullptr);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        WDF_OBJECT_ATTRIBUTES  attributes;
        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = request;
        WDFSTRING deviceName;
        status = WdfStringCreate(nullptr, &attributes, &deviceName);
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

        info->NameLength = uniDeviceName.Length;

        auto requiredBufferLength = FIELD_OFFSET(MOUNTDEV_NAME, Name) + static_cast<LONG>(uniDeviceName.Length);
        if (requiredBufferLength > outputBufferLength)
        {
            status = STATUS_BUFFER_OVERFLOW;
            bytesWritten = sizeof(*info);
        }
        else
        {
            memcpy(info->Name, uniDeviceName.Buffer, uniDeviceName.Length);
            bytesWritten = requiredBufferLength;
        }
        break;
    }

    case IOCTL_MOUNTDEV_QUERY_UNIQUE_ID:
    {
        MOUNTDEV_UNIQUE_ID* info;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(*info), reinterpret_cast<PVOID*>(&info), nullptr);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        WDF_DEVICE_PROPERTY_DATA devPropData{};
        WDF_DEVICE_PROPERTY_DATA_INIT(&devPropData, &DEVPKEY_Device_InstanceId);

        WDFMEMORY deviceInstanceId{};
        DEVPROPTYPE devPropType;
        WDF_OBJECT_ATTRIBUTES  attributes;
        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = request;
        status = WdfDeviceAllocAndQueryPropertyEx(WdfIoQueueGetDevice(queue), &devPropData, PagedPool, &attributes, &deviceInstanceId, &devPropType);
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

        info->UniqueIdLength = uniInstanceId.Length;

        auto requiredBufferLength = FIELD_OFFSET(MOUNTDEV_UNIQUE_ID, UniqueId) + static_cast<LONG>(uniInstanceId.Length);
        if (requiredBufferLength > outputBufferLength)
        {
            status = STATUS_BUFFER_OVERFLOW;
            bytesWritten = sizeof(*info);
        }
        else
        {
            memcpy(info->UniqueId, uniInstanceId.Buffer, uniInstanceId.Length);
            bytesWritten = requiredBufferLength;
        }
        break;
    }

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
    }

    WdfRequestCompleteWithInformation(request, status, bytesWritten);
}
