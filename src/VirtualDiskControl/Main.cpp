#include "pch.h"
#include "SwVirtualDevice.h"

namespace fs = std::filesystem;
using namespace std;

void printHelp()
{
    cout
        << "Virtual disk control utility. Copyright(C) 2022 Apriorit, Inc." << endl
        << endl
        << "Usage: " << endl
        << "  VirtualDiskControl open <filepath> [filesize] - Open an existing disk image or create a new one" << endl
        << "                                                  with the size `filesize` MB." << endl
        << "                                                  `filesize` is optional, default value is 100." << endl
        << "  VirtualDiskControl close <filepath>           - Close a disk image." << endl;
}

int wmain(int argc, wchar_t* argv[]) try
{
    if (argc < 3)
    {
        printHelp();
        return EXIT_FAILURE;
    }

    wstring_view command = argv[1];
    auto absoluteFilePath = fs::absolute(argv[2]);

    if (command == L"open")
    {
        if (!fs::exists(absoluteFilePath))
        {
            ofstream{ absoluteFilePath };

            const int kDefaultFileSize = 100; // 100MB
            const int fileSize = (argc >= 4 ? _wtoi(argv[3]) : kDefaultFileSize) * 1024 * 1024;

            fs::resize_file(absoluteFilePath, fileSize);
        }

        SwVirtualDevice device{ absoluteFilePath.c_str() };
        device.setLifetime(SWDeviceLifetimeParentPresent);
    }
    else if (command == L"close")
    {
        if (!fs::exists(absoluteFilePath))
        {
            cout << "The specified file doesn't exist." << endl;
            return EXIT_FAILURE;
        }

        SwVirtualDevice device{ absoluteFilePath.c_str() };
        device.setLifetime(SWDeviceLifetimeHandle);
    }
    else
    {
        printHelp();
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
catch (const exception& ex)
{
    cout << "Error: " << ex.what() << endl;
    return EXIT_FAILURE;
}
