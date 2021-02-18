#include <windows.h>
#include <swdevice.h>
#include <swdevicedef.h>
#include <iostream>
#include <functional>
#include <string>
#include <fstream>
#include <filesystem>
#include <conio.h>
#include <synchapi.h>

void WINAPI SwDeviceCreateCallback(
    HSWDEVICE hSwDevice,
    HRESULT CreateResult,
    PVOID pContext,
    PCWSTR pszDeviceInstanceId
)
{
    HANDLE hEvent = *(HANDLE*)pContext;
    SetEvent(hEvent);

    UNREFERENCED_PARAMETER(hSwDevice);
    UNREFERENCED_PARAMETER(CreateResult);
    UNREFERENCED_PARAMETER(pszDeviceInstanceId);
}

HSWDEVICE createDevice(const wchar_t* filePath)
{
    HSWDEVICE hSwDevice = NULL;
    HANDLE hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!hEvent)
    {
        std::cout << "CreateEvent failed." << std::endl;
        return hSwDevice;
    }

    auto instanceId = std::to_wstring(std::hash<std::wstring>{}(filePath));
    SW_DEVICE_CREATE_INFO deviceCreateInfo{ 0 };
    PCWSTR description = L"VirtualDisk Device";
    PCWSTR hardwareIds = L"Root\\VirtualDisk";
    PCWSTR compatibleIds = L"Root\\VirtualDisk";

    deviceCreateInfo.cbSize = sizeof(deviceCreateInfo);
    deviceCreateInfo.pszzCompatibleIds = compatibleIds;
    deviceCreateInfo.pszInstanceId = instanceId.c_str();
    deviceCreateInfo.pszzHardwareIds = hardwareIds;
    deviceCreateInfo.pszDeviceDescription = description;
    deviceCreateInfo.CapabilityFlags = SWDeviceCapabilitiesRemovable | SWDeviceCapabilitiesDriverRequired;

    HRESULT hr = SwDeviceCreate(L"VirtualDisk Device", L"HTREE\\ROOT\\0", &deviceCreateInfo, 0, NULL, SwDeviceCreateCallback, &hEvent, &hSwDevice);
    if (FAILED(hr))
    {
        std::cout << "SwDeviceCreate failed with the error code: " << hr << std::endl;
        return hSwDevice;
    }

    // Wait for callback to signal that the device has been created
    std::cout << "Waiting for device to be created..." << std::endl;
    DWORD waitResult = WaitForSingleObject(hEvent, INFINITE);
    if (waitResult != WAIT_OBJECT_0)
    {
        std::cout << "Wait for device creation failed" << std::endl;
        return hSwDevice;
    }
    std::cout << "Device created." << std::endl;

    return hSwDevice;
}

int main(int argc, wchar_t* argv[])
{
    std::wstring command = argv[1];
    const wchar_t* filePath = argv[2];

    if (argc < 3 || (argc == 3 && command != L"open") || (argc == 4 && command != L"create"))
    {
        std::cout << "Correct using: " << std::endl <<
            "virtualdiskcontrol open <filepath> - open existing disk image" << std::endl <<
            "virtualdiskcontrol create <filepath> <size> - create and open new disk image" << std::endl;
        return 1;
    }
    if (argc == 3 && command == L"open")
    {
        std::fstream existingFile;
        if (std::filesystem::exists(filePath))
        {
            existingFile.open(filePath, std::fstream::in | std::fstream::out | std::fstream::app);
        }

        if (existingFile.is_open())
        {
            existingFile.close();
        }
    }

    if (argc == 4 && command == L"create")
    {
        const wchar_t* sizeChar = argv[3];

        if (std::filesystem::exists(filePath))
        {
            std::cout << "The file exists. Using: virtualdiskcontrol open <filepath>" << std::endl;
        }
        else
        {
            std::fstream newFile(filePath);
            size_t sizeInt = _wtoi(sizeChar);
            std::filesystem::resize_file(filePath, sizeInt);
            if (newFile.is_open())
            {
                newFile.close();
            }
        }
    }

    HSWDEVICE hSwDevice = createDevice(filePath);
    if (!hSwDevice)
    {
        std::cout << "createDevice failed" << std::endl;
        return -1;
    }

    std::cout << "Press any key to to remove a device" << std::endl;
    if (_kbhit())
    {
        SwDeviceClose(hSwDevice);
    }

    return 0;
}
