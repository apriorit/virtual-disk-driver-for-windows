#pragma once

class Driver
{
public:
	static NTSTATUS create (_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath);

private:
	Driver() = default;
	~Driver() = default;

	Driver(const Driver&) = delete;
	Driver& operator= (const Driver&) = delete;

	static NTSTATUS evtDeviceAdd(_In_ WDFDRIVER driver, _In_ PWDFDEVICE_INIT deviceInit);
};
