//
// Created by Bryce Wilson on 1/6/21.
//

#ifndef BYTEFALL_STDLIB_H
#define BYTEFALL_STDLIB_H

#include <stddef.h>

extern "C" void* malloc(size_t Size) __attribute__((__warn_unused_result__)) __attribute__((alloc_size(1)));
extern "C" void free(void*);

#endif //BYTEFALL_STDLIB_H
