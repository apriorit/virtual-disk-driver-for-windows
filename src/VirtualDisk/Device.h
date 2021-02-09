#pragma once

class Device
{
public:
    static NTSTATUS create(_In_ WDFDRIVER wdfDriver, _Inout_ PWDFDEVICE_INIT deviceInit);
    
private:
    Device(_In_ WDFDEVICE device);
    ~Device() = default;

    static NTSTATUS init(WDFDEVICE hDevice, Device* self);
    static void onDeviceContextCleanup(_In_ WDFOBJECT wdfDevice);
    static void onIoRead(WDFQUEUE queue, WDFREQUEST request, size_t length);
    static void onIoWrite(WDFQUEUE queue, WDFREQUEST request, size_t length);
    static void onIoReadForward(WDFQUEUE queue, WDFREQUEST request, size_t length);
    static void onIoWriteForward(WDFQUEUE queue, WDFREQUEST request, size_t length);
    static void onIoDeviceControl(_In_ WDFQUEUE queue, _In_ WDFREQUEST request, _In_ size_t outputBufferLength, _In_ size_t inputBufferLength, _In_ ULONG ioControlCode);

private:
    HANDLE m_fileHandle;
    LARGE_INTEGER m_fileSize;
    WDFQUEUE m_fileQueue;
};


