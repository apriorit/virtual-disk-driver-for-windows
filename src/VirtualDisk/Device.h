#pragma once

class Device
{
public:
    static NTSTATUS create(_In_ WDFDRIVER wdfDriver, _Inout_ PWDFDEVICE_INIT deviceInit);

private:
    Device() = default;
    ~Device() = default;

    NTSTATUS init(WDFDEVICE hDevice, IO_STATUS_BLOCK& ioStatusBlock);
    static void onDeviceContextCleanup(_In_ WDFOBJECT wdfDevice);
    static void onIoRead(WDFQUEUE queue, WDFREQUEST request, size_t length);
    static void onIoWrite(WDFQUEUE queue, WDFREQUEST request, size_t length);
    static void onIoReadWriteForward(WDFQUEUE queue, WDFREQUEST request, size_t);
    static void onIoDeviceControl(_In_ WDFQUEUE queue, _In_ WDFREQUEST request, _In_ size_t outputBufferLength, _In_ size_t, _In_ ULONG ioControlCode);

private:
    HANDLE m_fileHandle;
    LARGE_INTEGER m_fileSize;
    WDFQUEUE m_fileQueue;
    static long m_counter;
};
