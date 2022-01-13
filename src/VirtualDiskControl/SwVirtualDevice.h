#pragma once

class SwVirtualDevice
{
public:
    SwVirtualDevice(const wchar_t* filePath);
    ~SwVirtualDevice();

    SwVirtualDevice(const SwVirtualDevice&) = delete;
    SwVirtualDevice& operator=(const SwVirtualDevice&) = delete;

    void setLifetime(SW_DEVICE_LIFETIME lifetime);

private:
    struct CallbackData
    {
        CEvent event{ false, false };
        HRESULT hr{ S_OK };

        void complete(HRESULT hr)
        {
            this->hr = hr;
            SetEvent(event);
        }
    };

private:
    HSWDEVICE m_handle{};
};
