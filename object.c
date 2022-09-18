#include "object.h"

#include <stdint.h>
#include <malloc.h>

void* object_create(size_t type_size)
{
    void* object = malloc(type_size + sizeof(struct ref));
    struct ref* object_ref = (struct ref*)(object);

    object_ref->count = 0;
    object_ref->free = 0;

    return ((uint8_t*)object) + sizeof(struct ref);
}

void* object_ref(void* object)
{
    if (!object) 
    {
        return NULL;
    }

    struct ref* object_ref = (void*)(((uint8_t*)object) - sizeof(struct ref));

    ++object_ref->count;

    return object;
}

void object_deref(void* object)
{
    struct ref* object_ref = (void*)(((uint8_t*)object) - sizeof(struct ref));

    if (--object_ref->count == 0)
    {
        if (object_ref->free)
        {
            object_ref->free(object_ref);
        }

        free(object);
    }
}