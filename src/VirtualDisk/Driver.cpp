#include "pch.h"
#include "Driver.h"
#include "Device.h"

//NTSTATUS Driver::create(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
//{
//	NTSTATUS            status = STATUS_SUCCESS;
//	WDF_DRIVER_CONFIG   config;
//
//	WDF_DRIVER_CONFIG_INIT(&config, EvtDeviceAdd);
//
//    status = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES,  &config, WDF_NO_HANDLE);
//
//	return status;
//
//}

//NTSTATUS Driver::EvtDeviceAdd(_In_ WDFDRIVER Driver, _In_ PWDFDEVICE_INIT DeviceInit)
//{
//    /*NTSTATUS                  status = STATUS_SUCCESS;
//    PFDO_DATA                 fdoData;
//    WDF_IO_QUEUE_CONFIG       queueConfig;
//    WDF_OBJECT_ATTRIBUTES     fdoAttributes;
//    WDFDEVICE                 hDevice;
//    WDFQUEUE                  queue;
//    UNICODE_STRING            uniName;
//    OBJECT_ATTRIBUTES         objAttr;
//    HANDLE                    handle;
//    IO_STATUS_BLOCK           ioStatusBlock;
//    FILE_STANDARD_INFORMATION fileInformation = { 0 };
//
//    WDF_IO_QUEUE_CONFIG       newQueueConfig;
//    WDFQUEUE                  newQueue;
//    WDF_OBJECT_ATTRIBUTES     queueAttributes;
//    UNICODE_STRING            deviceName;
//    UNICODE_STRING            symbolicLinkName;*/
//
//
//}