#include "pch.h"
#include "SwVirtualDevice.h"
#include <initguid.h>
#include "PropertyKeys.h"

using namespace std;

const wchar_t kDeviceDescription[] = L"VirtualDisk Device";
const wchar_t kHardwareIds[] = L"Root\\AprioritVirtualDisk\0";
const wchar_t kEnumeratorName[] = L"ROOT";
const wchar_t kParentDeviceInstance[] = L"HTREE\\ROOT\\0";

SwVirtualDevice::SwVirtualDevice(const wchar_t* filePath)
{
    const auto instanceId = to_wstring(hash<wstring>{}(filePath));
    wcout << "Device instance id: " << instanceId << endl;

    const SW_DEVICE_CREATE_INFO deviceCreateInfo
    {
        .cbSize = sizeof(deviceCreateInfo),
        .pszInstanceId = instanceId.c_str(),
        .pszzHardwareIds = kHardwareIds,
        .pszzCompatibleIds = nullptr,
        .pContainerId = nullptr,
        .CapabilityFlags = SWDeviceCapabilitiesRemovable | SWDeviceCapabilitiesDriverRequired,
        .pszDeviceDescription = kDeviceDescription,
    };

    const auto fullFilePath = L"\\??\\"s + filePath;
    wcout << "Full file path: " << fullFilePath << endl;

    const DEVPROPERTY devPropFilePath
    {
        .CompKey = { DEVPKEY_VIRTUALDISK_FILEPATH, DEVPROP_STORE_SYSTEM, 0 },
        .Type = DEVPROP_TYPE_STRING,
        .BufferSize = static_cast<ULONG>((fullFilePath.size() + 1) * sizeof(wchar_t)),
        .Buffer = const_cast<wchar_t*>(fullFilePath.c_str()),
    };

    CallbackData callbackData;
    HRESULT hr = SwDeviceCreate(kEnumeratorName,
        kParentDeviceInstance,
        &deviceCreateInfo,
        1,
        &devPropFilePath,
        [](HSWDEVICE, HRESULT result, PVOID ctx, PCWSTR) { static_cast<CallbackData*>(ctx)->complete(result); },
        &callbackData,
        &m_handle);
    if (FAILED(hr))
    {
        throw runtime_error("SwDeviceCreate failed with the error code: "s + to_string(hr));
    }

    // Wait for callback to signal that the device has been created
    WaitForSingleObject(callbackData.event, INFINITE);

    if (FAILED(callbackData.hr))
    {
        SwDeviceClose(m_handle);
        throw runtime_error("Device creation failed with the error code: "s + to_string(callbackData.hr));
    }
}

SwVirtualDevice::~SwVirtualDevice()
{
    SwDeviceClose(m_handle);
}

void SwVirtualDevice::setLifetime(SW_DEVICE_LIFETIME lifetime)
{
    HRESULT hr = SwDeviceSetLifetime(m_handle, lifetime);
    if (FAILED(hr))
    {
        throw runtime_error("SwDeviceSetLifetime failed with the error code: "s + to_string(hr));
    }
}
