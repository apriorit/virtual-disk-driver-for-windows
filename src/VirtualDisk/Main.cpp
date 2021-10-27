#include "pch.h"
#include "Driver.h"

////////////////////////////////////////////////////////////////////////
// Entry point

EXTERN_C DRIVER_INITIALIZE DriverEntry;

EXTERN_C NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT driverObject, _In_ PUNICODE_STRING registryPath)
{
    return Driver::create(driverObject, registryPath);
}
