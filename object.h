#pragma once

#include <stddef.h>

struct ref {
    void (*free)(const void* object);
    int count;
};

void* object_create(size_t type_size);
void* object_ref(void* object);
void object_deref(void* object);