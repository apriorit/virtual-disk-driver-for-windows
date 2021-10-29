#pragma once

class Driver
{
public:
    Driver() = delete;
    static NTSTATUS create(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath);

private:
    static NTSTATUS onDeviceAdd(WDFDRIVER wdfDriver, PWDFDEVICE_INIT deviceInit);
};
