#include "pch.h"
#include "Driver.h"
#include "Device.h"

NTSTATUS Driver::create(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, onDeviceAdd);

    return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
}

NTSTATUS Driver::onDeviceAdd(_In_ WDFDRIVER wdfDriver, _In_ PWDFDEVICE_INIT deviceInit)
{
    NTSTATUS status = Device::create(wdfDriver, deviceInit);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    return STATUS_SUCCESS;
}
