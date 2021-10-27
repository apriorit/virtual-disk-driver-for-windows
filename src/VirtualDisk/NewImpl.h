#pragma once

//
// Operator `new` support for a driver code
//

void* __cdecl operator new(size_t, void* ptr);
void __cdecl operator delete(void*, size_t);
