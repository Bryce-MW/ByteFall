//
// Created by Bryce Wilson on 1/8/21.
//

#ifndef BYTEFALL_STRING_H
#define BYTEFALL_STRING_H

#include <stddef.h>

extern "C" void* memmove(void* dest, const void* src, size_t length);
extern "C" void* memset(void* dest, int pattern, size_t length);
extern "C" void* memcpy(void* dest, const void* src, size_t length);

#endif //BYTEFALL_STRING_H
