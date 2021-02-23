#include "pch.h"

DEFINE_DEVPROPKEY(DEVPKEY_FILEPATH, 0x8792f614, 0x3667, 0x4df0, 0x95, 0x49, 0x3a, 0xc6, 0x4b, 0x51, 0xa0, 0xdb, 2);

const wchar_t* deviceDesc = L"VirtualDisk Device";
const wchar_t* hwId = L"Root\\VirtualDisk\0\0";

void WINAPI SwDeviceCreateCallback(
    HSWDEVICE hSwDevice,
    HRESULT CreateResult,
    PVOID pContext,
    PCWSTR pszDeviceInstanceId
)
{
    HANDLE hEvent = *(HANDLE*)pContext;
    SetEvent(hEvent);

    HRESULT hr = CreateResult;
    if (FAILED(hr))
    {
        std::cout << "CreateResult argument in SwDeviceCreateCallback is invalid, the error code: " << hr << std::endl;
        return;
    }
    UNREFERENCED_PARAMETER(hSwDevice);
    UNREFERENCED_PARAMETER(pszDeviceInstanceId);
}

HSWDEVICE createDevice(const wchar_t* filePath)
{
    HSWDEVICE hSwDevice = nullptr;

    HANDLE hEvent = CreateEvent(nullptr, false, false, nullptr);
    if (!hEvent)
    {
        std::cout << "CreateEvent failed." << std::endl;
        return hSwDevice;
    }

    auto instanceId = std::to_wstring(std::hash<std::wstring>{}(filePath));
    SW_DEVICE_CREATE_INFO deviceCreateInfo{ 0 };
    PCWSTR description = deviceDesc;
    PCWSTR hardwareIds = hwId;

    deviceCreateInfo.cbSize = sizeof(deviceCreateInfo);
    deviceCreateInfo.pszzCompatibleIds = nullptr;
    deviceCreateInfo.pszInstanceId = instanceId.c_str();
    deviceCreateInfo.pszzHardwareIds = hardwareIds;
    deviceCreateInfo.pszDeviceDescription = description;
    deviceCreateInfo.CapabilityFlags = SWDeviceCapabilitiesRemovable | SWDeviceCapabilitiesDriverRequired;

    DEVPROPCOMPKEY propCompoundKey{0};
    propCompoundKey.Key = DEVPKEY_FILEPATH;
    propCompoundKey.Store = DEVPROP_STORE_SYSTEM;

    DEVPROPERTY devPropFilePath{};
    devPropFilePath.CompKey = propCompoundKey;
    devPropFilePath.Type = DEVPROP_TYPE_STRING;
    devPropFilePath.BufferSize = (wcslen(filePath)+1) * sizeof(wchar_t);
    devPropFilePath.Buffer = (PVOID)filePath;
    DEVPROPERTY devPropsArray[] = { devPropFilePath };

    HRESULT hr = SwDeviceCreate(L"ROOT", L"HTREE\\ROOT\\0", &deviceCreateInfo, 1, devPropsArray, SwDeviceCreateCallback, &hEvent, &hSwDevice);
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
        std::cout << "Wait for device creation failed." << std::endl;
        return hSwDevice;
    }
    std::cout << "Device created." << std::endl;

    return hSwDevice;
}

int wmain(int argc, wchar_t* argv[])
{
    std::wstring command{};
    const wchar_t* filePath{};

    switch (argc)
    {
    case 3:
    case 4:
    {
        command = argv[1];
        filePath = argv[2];

        if (command == L"open")
        {
            std::fstream existingFile;
            if (std::filesystem::exists(filePath))
            {
                existingFile.open(filePath, std::fstream::in | std::fstream::out | std::fstream::app);
            }
            else
            {
                std::cout << "The file doesn`t exist. Using: virtualdiskcontrol create <filepath> <size>" << std::endl;
                return 1;
            }
        }
        else if (command == L"create")
        {
            const wchar_t* sizeChar = argv[3];

            if (std::filesystem::exists(filePath))
            {
                std::cout << "The file exists. Using: virtualdiskcontrol open <filepath>" << std::endl;
                return 1;
            }
            else
            {
                std::ofstream newFile{filePath};
                size_t sizeInt = _wtoi(sizeChar);
                if (std::filesystem::exists(filePath))
                {
                    std::filesystem::resize_file(filePath, sizeInt);
                }
            }
        }
        else
        {
            std::cout << "Second parameter must be open or create." << std::endl;
            return 1;
        }
        break;
    }

    default:
        std::cout << "Correct using: " << std::endl <<
            "virtualdiskcontrol open <filepath> - open existing disk image" << std::endl <<
            "virtualdiskcontrol create <filepath> <size> - create and open new disk image" << std::endl;
        return 1;
    }

    HSWDEVICE hSwDevice = createDevice(filePath);
    if (!hSwDevice)
    {
        std::cout << "createDevice failed." << std::endl;
        return -1;
    }

    std::cout << "Press 1 to remove a device." << std::endl;
    bool close = false;
    std::cin >> close;

    SwDeviceClose(hSwDevice);

    return 0;
}
