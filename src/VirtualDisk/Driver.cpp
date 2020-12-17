#include "pch.h"
#include "Driver.h"
#include "Device.h"

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(Device, DeviceGetData)

NTSTATUS Driver::create(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
	WDF_DRIVER_CONFIG config;
	WDF_DRIVER_CONFIG_INIT(&config, evtDeviceAdd);

    NTSTATUS status = STATUS_SUCCESS;
    status = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES,  &config, WDF_NO_HANDLE);
	return status;
}

NTSTATUS Driver::evtDeviceAdd(_In_ WDFDRIVER wdfDriver, _In_ PWDFDEVICE_INIT deviceInit)
{
    UNREFERENCED_PARAMETER(wdfDriver);

    NTSTATUS status = Device::create(deviceInit);
    if (!NT_SUCCESS(status))
    {
        return status;
    }
    return STATUS_SUCCESS;
}



