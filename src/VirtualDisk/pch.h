#pragma once
#define _NO_CRT_STDIO_INLINE  // https://stackoverflow.com/a/67745800/122951
#include <ntddk.h>
#include <wdf.h>
#include <ntstrsafe.h>
#include <ntdddisk.h>
#include <initguid.h>
#include <ntddstor.h>
#include <ntddscsi.h>
#include <mountmgr.h>
#include <mountdev.h>
