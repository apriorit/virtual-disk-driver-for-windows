#include "pch.h"
#include "Device.h"
#include "PropertyKeys.h"
#include "NewImpl.h"

// Declare context and getter function
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(Device, getDevice)

// Additional getter function
static Device* getDevice(WDFQUEUE queue)
{
    return getDevice(WdfIoQueueGetDevice(queue));
}

long Device::m_counter = 0;

NTSTATUS Device::create(_Inout_ PWDFDEVICE_INIT deviceInit)
{
    //
    // Set device type
    //

    WdfDeviceInitSetDeviceType(deviceInit, FILE_DEVICE_DISK);
    WdfDeviceInitSetIoType(deviceInit, WdfDeviceIoDirect);

    //
    // Set device name
    //

    const int kDeviceNameMaxLenght = sizeof(L"\\Device\\AprioritVirtualDisk-00000000");
    wchar_t deviceNameBuffer[kDeviceNameMaxLenght];

    UNICODE_STRING deviceName;
    RtlInitEmptyUnicodeString(&deviceName, deviceNameBuffer, sizeof(deviceNameBuffer));
    RtlUnicodeStringPrintf(&deviceName, L"\\Device\\AprioritVirtualDisk-%x", InterlockedIncrement(&m_counter));

    NTSTATUS status = WdfDeviceInitAssignName(deviceInit, &deviceName);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    //
    // Set device permissions
    //

    static const UNICODE_STRING sddl = RTL_CONSTANT_STRING(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGWGXSD;;;WD)");
    status = WdfDeviceInitAssignSDDLString(deviceInit, &sddl);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    //
    // Create device
    //

    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, Device);
    deviceAttributes.EvtCleanupCallback = onCleanup;

    WDFDEVICE wdfDevice;
    status = WdfDeviceCreate(&deviceInit, &deviceAttributes, &wdfDevice);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    //
    // Initialize device
    //

    auto self = new(getDevice(wdfDevice)) Device();

    status = self->init(wdfDevice);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    return STATUS_SUCCESS;
}

void Device::onCleanup(WDFOBJECT wdfDevice)
{
    getDevice(reinterpret_cast<WDFDEVICE>(wdfDevice))->~Device();
}

Device::~Device()
{
    if (m_fileHandle)
    {
        ZwClose(m_fileHandle);
    }
}

NTSTATUS Device::init(WDFDEVICE wdfDevice)
{
    //
    // Get file path from device property data
    //

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = reinterpret_cast<WDFOBJECT>(wdfDevice);

    WDF_DEVICE_PROPERTY_DATA propertyData;
    WDF_DEVICE_PROPERTY_DATA_INIT(&propertyData, &DEVPKEY_VIRTUALDISK_FILEPATH);

    WDFMEMORY propertyMemory{};
    DEVPROPTYPE propertyType{};
    NTSTATUS status = WdfDeviceAllocAndQueryPropertyEx(wdfDevice, &propertyData, PagedPool, &attributes, &propertyMemory, &propertyType);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    size_t filePathBufferSize{};
    PVOID filePathBuffer = WdfMemoryGetBuffer(propertyMemory, &filePathBufferSize);

    UNICODE_STRING filePath
    {
        .Length = static_cast<USHORT>(filePathBufferSize - sizeof(wchar_t)),
        .MaximumLength = static_cast<USHORT>(filePathBufferSize),
        .Buffer = reinterpret_cast<PWCH>(filePathBuffer)
    };

    //
    // Open a disk image file
    //

    OBJECT_ATTRIBUTES oa = RTL_INIT_OBJECT_ATTRIBUTES(&filePath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE);
    IO_STATUS_BLOCK iosb;

    status = ZwOpenFile(&m_fileHandle, GENERIC_READ | GENERIC_WRITE, &oa, &iosb, 0, FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    //
    // Query a disk image file size
    //

    FILE_STANDARD_INFORMATION fileInformation{};
    status = ZwQueryInformationFile(m_fileHandle, &iosb, &fileInformation, sizeof(fileInformation), FileStandardInformation);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    m_fileSize = fileInformation.EndOfFile;

    //
    // Create a device interface
    //

    status = WdfDeviceCreateDeviceInterface(wdfDevice, &GUID_DEVINTERFACE_VOLUME, nullptr);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    //
    // Create a default queue
    //

    WDF_IO_QUEUE_CONFIG defaultQueueConfig;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&defaultQueueConfig, WdfIoQueueDispatchParallel);
    defaultQueueConfig.EvtIoRead = onIoReadWriteForward;
    defaultQueueConfig.EvtIoWrite = onIoReadWriteForward;
    defaultQueueConfig.EvtIoDeviceControl = onIoDeviceControl;

    status = WdfIoQueueCreate(wdfDevice, &defaultQueueConfig, WDF_NO_OBJECT_ATTRIBUTES, nullptr);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    //
    // Create a file i/o queue
    //

    WDF_IO_QUEUE_CONFIG fileQueueConfig;
    WDF_IO_QUEUE_CONFIG_INIT(&fileQueueConfig, WdfIoQueueDispatchParallel);
    fileQueueConfig.EvtIoRead = onIoRead;
    fileQueueConfig.EvtIoWrite = onIoWrite;

    WDF_OBJECT_ATTRIBUTES fileQueueAttributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&fileQueueAttributes);
    fileQueueAttributes.ExecutionLevel = WdfExecutionLevelPassive;

    status = WdfIoQueueCreate(wdfDevice, &fileQueueConfig, &fileQueueAttributes, &m_fileQueue);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    return STATUS_SUCCESS;
}

void Device::onIoRead(WDFQUEUE queue, WDFREQUEST request, size_t length)
{
    //
    // Get buffer and parameters
    //

    PVOID outputBuffer;
    NTSTATUS status = WdfRequestRetrieveOutputBuffer(request, 0, &outputBuffer, nullptr);
    if (!NT_SUCCESS(status))
    {
        WdfRequestCompleteWithInformation(request, status, 0);
        return;
    }

    WDF_REQUEST_PARAMETERS requestParams;
    WDF_REQUEST_PARAMETERS_INIT(&requestParams);
    WdfRequestGetParameters(request, &requestParams);

    //
    // Read from file
    //

    IO_STATUS_BLOCK iosb{};
    status = ZwReadFile(getDevice(queue)->m_fileHandle,
        nullptr,
        nullptr,
        nullptr,
        &iosb,
        outputBuffer,
        static_cast<ULONG>(length),
        reinterpret_cast<PLARGE_INTEGER>(&requestParams.Parameters.Read.DeviceOffset),
        nullptr);
    WdfRequestCompleteWithInformation(request, status, iosb.Information);
}

void Device::onIoWrite(WDFQUEUE queue, WDFREQUEST request, size_t length)
{
    //
    // Get buffer and parameters
    //

    PVOID inputBuffer;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(request, 0, &inputBuffer, nullptr);
    if (!NT_SUCCESS(status))
    {
        WdfRequestCompleteWithInformation(request, status, 0);
        return;
    }

    WDF_REQUEST_PARAMETERS requestParams;
    WDF_REQUEST_PARAMETERS_INIT(&requestParams);
    WdfRequestGetParameters(request, &requestParams);

    //
    // Write to file
    //

    IO_STATUS_BLOCK iosb{};
    status = ZwWriteFile(getDevice(queue)->m_fileHandle,
        nullptr,
        nullptr,
        nullptr,
        &iosb,
        inputBuffer,
        static_cast<ULONG>(length),
        reinterpret_cast<PLARGE_INTEGER>(&requestParams.Parameters.Write.DeviceOffset),
        nullptr);
    WdfRequestCompleteWithInformation(request, status, iosb.Information);
}

void Device::onIoReadWriteForward(WDFQUEUE queue, WDFREQUEST request, size_t)
{
    //
    // Forward read/write requests to the file i/o queue. To force processing in another thread raise IRQL.
    //

    KIRQL oldIrql;
    KeRaiseIrql(DISPATCH_LEVEL, &oldIrql);
    WdfRequestForwardToIoQueue(request, getDevice(queue)->m_fileQueue);
    KeLowerIrql(oldIrql);
}

void Device::onIoDeviceControl(WDFQUEUE queue, WDFREQUEST request, size_t outputBufferLength, size_t, ULONG ioControlCode)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG_PTR bytesWritten = 0;
    auto self = getDevice(queue);

    //
    // Handle required control codes
    //

    switch (ioControlCode)
    {
    case IOCTL_STORAGE_GET_DEVICE_NUMBER:
    {
        STORAGE_DEVICE_NUMBER* info;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(*info), reinterpret_cast<void**>(&info), nullptr);
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
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(*info), reinterpret_cast<void**>(&info), nullptr);
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
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(*info), reinterpret_cast<void**>(&info), nullptr);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        info->Length = self->m_fileSize;

        bytesWritten = sizeof(*info);
        break;
    }

    case IOCTL_DISK_GET_MEDIA_TYPES:
    case IOCTL_DISK_GET_DRIVE_GEOMETRY:
    {
        DISK_GEOMETRY* info;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(*info), reinterpret_cast<void**>(&info), nullptr);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        info->BytesPerSector = 512;
        info->SectorsPerTrack = 1;
        info->TracksPerCylinder = 1;
        info->Cylinders.QuadPart = (self->m_fileSize.QuadPart + info->BytesPerSector - 1) / info->BytesPerSector;
        info->MediaType = RemovableMedia;

        bytesWritten = sizeof(*info);
        break;
    }

    case IOCTL_DISK_IS_WRITABLE:
        break;

    case IOCTL_MOUNTDEV_QUERY_DEVICE_NAME:
    {
        MOUNTDEV_NAME* info;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(*info), reinterpret_cast<void**>(&info), nullptr);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        WDF_OBJECT_ATTRIBUTES attributes;
        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = request;

        WDFSTRING wdfDeviceName;
        status = WdfStringCreate(nullptr, &attributes, &wdfDeviceName);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        status = WdfDeviceRetrieveDeviceName(WdfIoQueueGetDevice(queue), wdfDeviceName);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        UNICODE_STRING deviceName;
        WdfStringGetUnicodeString(wdfDeviceName, &deviceName);

        info->NameLength = deviceName.Length;

        //
        // Return as much info as fits in the buffer size
        //

        auto requiredBufferLength = FIELD_OFFSET(MOUNTDEV_NAME, Name) + static_cast<ULONG>(deviceName.Length);
        if (requiredBufferLength > outputBufferLength)
        {
            status = STATUS_BUFFER_OVERFLOW;
            bytesWritten = sizeof(*info);
        }
        else
        {
            memcpy(info->Name, deviceName.Buffer, deviceName.Length);
            bytesWritten = requiredBufferLength;
        }
        break;
    }

    case IOCTL_MOUNTDEV_QUERY_UNIQUE_ID:
    {
        MOUNTDEV_UNIQUE_ID* info;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(*info), reinterpret_cast<void**>(&info), nullptr);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        WDF_DEVICE_PROPERTY_DATA propertyData{};
        WDF_DEVICE_PROPERTY_DATA_INIT(&propertyData, &DEVPKEY_Device_InstanceId);

        WDF_OBJECT_ATTRIBUTES attributes;
        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = request;

        WDFMEMORY propertyMemory{};
        DEVPROPTYPE propertyType{};
        status = WdfDeviceAllocAndQueryPropertyEx(WdfIoQueueGetDevice(queue), &propertyData, PagedPool, &attributes, &propertyMemory, &propertyType);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        size_t instanceIdBufferSize{};
        PVOID instanceIdBuffer = WdfMemoryGetBuffer(propertyMemory, &instanceIdBufferSize);

        UNICODE_STRING instanceId
        {
            .Length = static_cast<USHORT>(instanceIdBufferSize - sizeof(wchar_t)),
            .MaximumLength = static_cast<USHORT>(instanceIdBufferSize),
            .Buffer = reinterpret_cast<PWCH>(instanceIdBuffer)
        };

        info->UniqueIdLength = instanceId.Length;

        //
        // Return as much info as fits in the buffer size
        //

        auto requiredBufferLength = FIELD_OFFSET(MOUNTDEV_UNIQUE_ID, UniqueId) + static_cast<ULONG>(instanceId.Length);
        if (requiredBufferLength > outputBufferLength)
        {
            status = STATUS_BUFFER_OVERFLOW;
            bytesWritten = sizeof(*info);
        }
        else
        {
            memcpy(info->UniqueId, instanceId.Buffer, instanceId.Length);
            bytesWritten = requiredBufferLength;
        }
        break;
    }

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
    }

    WdfRequestCompleteWithInformation(request, status, bytesWritten);
}
