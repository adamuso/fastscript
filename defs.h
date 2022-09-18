#pragma once

#define MAX_IDENTIFIER_LENGTH 32

enum 
{
    // not sized because it will acquire size from incoming type,
    // part of 'let' keyword for variable definition, cannot be used
    // for example for struct fields
    STACK_TYPE_ACQUIRE,

    // ptr sized
    STACK_TYPE_STRUCT,
    STACK_TYPE_OBJECT,
    NATIVE_TYPE_PTR,
    NATIVE_TYPE_NATIVE_FUNCTION,

    // 8 bits -> 1 byte
    NATIVE_TYPE_I8,
    NATIVE_TYPE_U8,

    // 16 bits -> 2 bytes
    NATIVE_TYPE_I16,
    NATIVE_TYPE_U16,

    // 32 bits -> 4 bytes
    NATIVE_TYPE_U32,
    NATIVE_TYPE_I32,
    NATIVE_TYPE_FLOAT,
    NATIVE_TYPE_FUNCTION,

    // 64 bits -> 8 bytes
    NATIVE_TYPE_I64,
    NATIVE_TYPE_U64,
    NATIVE_TYPE_DOUBLE,
    
    // ???
    NATIVE_TYPE_VOID,
    NATIVE_TYPE_STRING,

    // size depends on the size of the structure
    STACK_TYPE_STRUCT_INSTANCE,
    STACK_TYPE_STRUCT_END,

    // 8th bit is specifying if variable has dynamic type, always 8 bits in size
    STACK_TYPE_DYNAMIC = 0x80
};
