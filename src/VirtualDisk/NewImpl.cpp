#include "pch.h"
#include "NewImpl.h"

void* __cdecl operator new(size_t, void* ptr)
{
    return ptr;
}

void __cdecl operator delete(void*, size_t)
{
}
