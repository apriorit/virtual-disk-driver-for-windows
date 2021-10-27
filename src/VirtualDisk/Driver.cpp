#include "pch.h"
#include "Driver.h"
#include "Device.h"

NTSTATUS Driver::create(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, onDeviceAdd);

    return WdfDriverCreate(driverObject, registryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
}

NTSTATUS Driver::onDeviceAdd(WDFDRIVER, PWDFDEVICE_INIT deviceInit)
{
    return Device::create(deviceInit);
}
