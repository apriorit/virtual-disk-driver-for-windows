#include "pch.h"
#include "DevPropKeys.h"
namespace fs = std::filesystem;

const wchar_t* gDeviceDesc = L"VirtualDisk Device";
const wchar_t* gHwId = L"Root\\VirtualDisk\0\0";
const wchar_t* gEnumeratorName = L"ROOT";
const wchar_t* gParentDeviceInstance = L"HTREE\\ROOT\\0";

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
    HSWDEVICE hSwDevice{};

    HANDLE hEvent = CreateEvent(nullptr, false, false, nullptr);
    if (!hEvent)
    {
        std::cout << "CreateEvent failed." << std::endl;
        return hSwDevice;
    }

    SW_DEVICE_CREATE_INFO deviceCreateInfo{};
    PCWSTR description = gDeviceDesc;
    PCWSTR hardwareIds = gHwId;

    deviceCreateInfo.cbSize = sizeof(deviceCreateInfo);
    auto instanceId = std::to_wstring(std::hash<std::wstring>{}(filePath));
    deviceCreateInfo.pszInstanceId = instanceId.c_str();
    deviceCreateInfo.pszzHardwareIds = hardwareIds;
    deviceCreateInfo.pszDeviceDescription = description;
    deviceCreateInfo.CapabilityFlags = SWDeviceCapabilitiesRemovable | SWDeviceCapabilitiesDriverRequired;

    DEVPROPCOMPKEY propCompoundKey{ DEVPKEY_VIRTUALDISK_FILEPATH, DEVPROP_STORE_SYSTEM, 0};

    DEVPROPERTY devPropFilePath{};
    devPropFilePath.CompKey = propCompoundKey;
    devPropFilePath.Type = DEVPROP_TYPE_STRING;
    std::wstring pref = L"\\??\\";
    std::wstring fullFilePath = pref + filePath;
    devPropFilePath.BufferSize = (fullFilePath.size() + 1) * sizeof(wchar_t);
    devPropFilePath.Buffer = static_cast<PVOID>(const_cast<wchar_t*>(fullFilePath.c_str()));

    HRESULT hr = SwDeviceCreate(gEnumeratorName, gParentDeviceInstance, &deviceCreateInfo, 1, &devPropFilePath, SwDeviceCreateCallback, &hEvent, &hSwDevice);
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
    fs::path absoluteFilePath{};

    switch (argc)
    {
    case 3:
    case 4:
    {
        command = argv[1];
        filePath = argv[2];
        absoluteFilePath = fs::absolute(filePath);

        if (command == L"open")
        {
            std::fstream existingFile;
            if (std::filesystem::exists(absoluteFilePath))
            {
                existingFile.open(absoluteFilePath, std::fstream::in | std::fstream::out | std::fstream::app);
            }
            else
            {
                std::cout << "The file doesn`t exist. Using: virtualdiskcontrol create <filepath> <size>" << std::endl;
                return 1;
            }
        }
        else if (command == L"create")
        {
            if (argc != 4)
            {
                std::cout << "Please enter size after create: virtualdiskcontrol create <filepath> <size>" << std::endl;
                return 1;
            }
            const wchar_t* fileSizeChar = argv[3];

            if (std::filesystem::exists(absoluteFilePath))
            {
                std::cout << "The file exists. Using: virtualdiskcontrol open <filepath>" << std::endl;
                return 1;
            }
            else
            {
                std::ofstream newFile{ absoluteFilePath };
                if (std::filesystem::exists(absoluteFilePath))
                {
                    std::filesystem::resize_file(absoluteFilePath, _wtoi(fileSizeChar));
                }
            }
        }
        else if (command == L"close")
        {
            if (std::filesystem::exists(absoluteFilePath))
            {
                HSWDEVICE hSwDevice = createDevice(absoluteFilePath.c_str());
                if (!hSwDevice)
                {
                    std::cout << "createDevice failed." << std::endl;
                    return -1;
                }

                HRESULT hr = SwDeviceSetLifetime(hSwDevice, SWDeviceLifetimeHandle);
                if (FAILED(hr))
                {
                    std::cout << "SwDeviceSetLifetime failed with the error code: " << hr << std::endl;
                    return -1;
                }

                SwDeviceClose(hSwDevice);

                std::cout << "Device closed." << std::endl;
            }
            else
            {
                std::cout << "The file doesn`t exist." << std::endl;
            }

            return 0;
        }
        else
        {
            std::cout << "Second parameter must be open, create or close." << std::endl;
            return 1;
        }
        break;
    }

    default:
        std::cout << "Correct using: " << std::endl <<
            "virtualdiskcontrol open <filepath> - open existing disk image" << std::endl <<
            "virtualdiskcontrol create <filepath> <size> - create and open new disk image" << std::endl<<
            "virtualdiskcontrol close <filepath> - close disk image" << std::endl;
        return 1;
    }

    HSWDEVICE hSwDevice = createDevice(absoluteFilePath.c_str());
    if (!hSwDevice)
    {
        std::cout << "createDevice failed." << std::endl;
        return -1;
    }

    HRESULT hr = SwDeviceSetLifetime(hSwDevice, SWDeviceLifetimeParentPresent);
    if (FAILED(hr))
    {
        std::cout << "SwDeviceSetLifetime failed with the error code: " << hr << std::endl;
        return -1;
    }

    SwDeviceClose(hSwDevice);

    return 0;
}
