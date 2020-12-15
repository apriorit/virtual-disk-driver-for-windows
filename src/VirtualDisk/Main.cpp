#include "pch.h"
#include "Driver.h"

//////////////////////////////////////////////////////////////////////////
// Entry point

EXTERN_C DRIVER_INITIALIZE DriverEntry;

EXTERN_C NTSTATUS DriverEntry(IN PDRIVER_OBJECT  /*DriverObject*/, IN PUNICODE_STRING /*RegistryPath*/)
{
    return STATUS_SUCCESS;
}