#pragma once

class Device
{
public:
    static NTSTATUS create(_Inout_ PWDFDEVICE_INIT deviceInit);

private:
    Device() = default;
    ~Device();

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    NTSTATUS init(WDFDEVICE wdfDevice);
    static void onCleanup(WDFOBJECT wdfDevice);
    static void onIoRead(WDFQUEUE queue, WDFREQUEST request, size_t length);
    static void onIoWrite(WDFQUEUE queue, WDFREQUEST request, size_t length);
    static void onIoReadWriteForward(WDFQUEUE queue, WDFREQUEST request, size_t);
    static void onIoDeviceControl(WDFQUEUE queue, WDFREQUEST request, size_t outputBufferLength, size_t, ULONG ioControlCode);

private:
    HANDLE m_fileHandle{};
    LARGE_INTEGER m_fileSize{};
    WDFQUEUE m_fileQueue{};
    static long m_counter;
};
