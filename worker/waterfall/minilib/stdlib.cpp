//
// Created by Bryce Wilson on 1/6/21.
//

#include "stdlib.h"

extern volatile u8 __heap_base;
static volatile u8* HeapBase = &__heap_base;

static size_t Malloc_Current = HeapBase + 512;

extern "C" void* malloc(size_t Size) {
    // NOTE(bryce): This will not grow memory!! It shouldn't be used which is why I have
    //  such a basic version. This is more of a just-in-case type definition. If I really
    //  need it then I will write something proper.
    size_t Result = Malloc_Current;
    Malloc_Current += Size;
    return (void*)Result;
}

extern "C" void free(void*) {
    // NOTE(bryce): I don't think that we call any functions that need to allocate so I will
    //  will just ignore this.
    return;
}
