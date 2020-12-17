#pragma once

class Device
{
public:
    static NTSTATUS create(_Inout_ PWDFDEVICE_INIT deviceInit);
    
private:
    Device(_In_ WDFDEVICE device);
    ~Device() = default;

    static NTSTATUS init(WDFDEVICE hDevice, Device* deviceData);
    static void evtDeviceContextCleanup(_In_ WDFOBJECT wdfDevice);
    static void evtIoRead(WDFQUEUE queue, WDFREQUEST request, size_t length);
    static void evtIoWrite(WDFQUEUE queue, WDFREQUEST request, size_t length);
    static void evtIoReadForward(WDFQUEUE queue, WDFREQUEST request, size_t length);
    static void evtIoWriteForward(WDFQUEUE queue, WDFREQUEST request, size_t length);
    static void evtIoDeviceControl(_In_ WDFQUEUE queue, _In_ WDFREQUEST request, _In_ size_t outputBufferLength, _In_ size_t inputBufferLength, _In_ ULONG ioControlCode);

private:
    HANDLE handle;
    LARGE_INTEGER fileSize;
    WDFQUEUE  customQueue;
};


