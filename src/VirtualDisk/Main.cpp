#include "pch.h"
#include "Driver.h"

//////////////////////////////////////////////////////////////////////////
// Entry point

EXTERN_C DRIVER_INITIALIZE DriverEntry;

EXTERN_C NTSTATUS DriverEntry(IN PDRIVER_OBJECT  driverObject, IN PUNICODE_STRING registryPath)
{
    return Driver::create(driverObject, registryPath);
}