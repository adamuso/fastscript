#define TEST_DEBUG
#define TOKEN_DEBUG

#ifdef TOKEN_DEBUG
const char* stack_type_names[] = 
{
    "STACK_TYPE_ACQUIRE",
    "STACK_TYPE_TYPEDEF",
    "STACK_TYPE_STRUCT",
    "STACK_TYPE_OBJECT",
    "NATIVE_TYPE_PTR",
    "NATIVE_TYPE_NATIVE_FUNCTION",
    "NATIVE_TYPE_I8",
    "NATIVE_TYPE_U8",
    "NATIVE_TYPE_I16",
    "NATIVE_TYPE_U16",
    "NATIVE_TYPE_I32",
    "NATIVE_TYPE_U32",
    "NATIVE_TYPE_FLOAT",
    "NATIVE_TYPE_FUNCTION",
    "NATIVE_TYPE_I64",
    "NATIVE_TYPE_U64",
    "NATIVE_TYPE_DOUBLE",
    "NATIVE_TYPE_VOID",
    "NATIVE_TYPE_STRING",
    "STACK_TYPE_STRUCT_INSTANCE",
    "STACK_TYPE_STRUCT_END",
    "STACK_TYPE_DYNAMIC"
};

const char* get_stack_type_name(int type)
{
    type = type & 0x7f;

    if (type < 0 || type >= 23) 
    {
        return "invalid_type";
    }

    return stack_type_names[type];
}
#endif