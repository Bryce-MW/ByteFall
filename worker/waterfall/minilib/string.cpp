//
// Created by Bryce Wilson on 1/8/21.
//

#include "string.h"
#include <stdint.h>


extern "C" void* memmove(void* dest, const void* src, size_t length) {
    if (dest < src) {
        auto* Source = (uint8_t*)src;
        auto* Dest = (uint8_t*)dest;
        while (length--) {
            *Dest++ = *Source++;
        }
    } else {
        auto* Source = (uint8_t*)src + length;
        auto* Dest = (uint8_t*)dest + length;
        while (length--) {
            *Dest-- = *Source--;
        }
    }
    return dest;
}

extern "C" void* memset(void* dest, int pattern, size_t length) {
    auto* Dest = (uint8_t*)dest;
    while (length--) {
        *Dest++ = (uint8_t)pattern;
    }
    return dest;
}

extern "C" void* memcpy(void* dest, const void* src, size_t length) {
    auto* Dest = (uint8_t*)dest;
    auto* Src = (uint8_t*)src;
    while (length--) {
        *Dest++ = *Src++;
    }
    return Dest;
}
