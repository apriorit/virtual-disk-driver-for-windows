#pragma once
#pragma warning(disable: 4471) // a forward declaration of an unscoped enumeration must have an underlying type
#define _NO_CRT_STDIO_INLINE  // https://stackoverflow.com/a/67745800/122951
#include <ntddk.h>
#include <wdf.h>
#include <ntstrsafe.h>
#include <ntdddisk.h>
#include <initguid.h>
#include <ntddstor.h>
#include <mountdev.h>
#include <devpkey.h>
