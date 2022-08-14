#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdbool.h>

#define TEST_DEBUG
#define TOKEN_DEBUG

enum 
{
    // not sized because it will acquire size from incoming type,
    // part of 'let' keyword for variable definition, cannot be used
    // for example for struct fields
    STACK_TYPE_ACQUIRE,

    // ptr sized
    STACK_TYPE_NATIVE_FUNCTION,
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
    STACK_TYPE_FUNCTION,
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

#ifdef TOKEN_DEBUG
const char* stack_type_names[6] = 
{
    "unknown",
    "integer",
    "native_function",
    "function",
    "struct",
    "object"
};

const char* get_stack_type_name(int type)
{
    type = type & 0x7f;

    if (type < 0 || type >= 6) 
    {
        return "invalid_type";
    }

    return stack_type_names[type];
}
#endif

struct ref {
    void (*free)(const void* object);
    int count;
};

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

#define MAX_IDENTIFIER_LENGTH 32

void exec(const char* code);

// Stack holds values with the size of native pointer, for 64 bit architectures: 8 bytes
// All native types should have a size that fits in a stack value
//
// Block: 
//   Starts with '{' and ends with '}', consists of one or more statements
// 
// Statement:
//   Ends with a semicolon ';'. Consists of an expression or variable declaration.   
//   After all statements stack should be cleared, but not after variable declarations
//
// Variable declaration:
//   Starts with: 'let', 'const', 'var' or type name.
//   Declaration types:
//   - let - declares a mutable variable which have static type that is acquired from the assignment
//   - const - declares a non mutable variable which have a static type that is acquired from the assignment
//   - var - declares a mutable variable which have dynamic type
//   - <type name> - declares a mutable variable which have explictly defined type
//

int main() 
{
    exec("\
        let X = struct { \
            i32 test; \
            \
            i32 new() => { \
                17 \
            } \
        }; \
        \
        var a = 2; \
        print(a); \
        a = add(a, 10); \
        print(a); \
        \
        var b = void(i32 x) => { \
            x = add(x, 5); \
            print(x); \
        }; \
        \
        b(a); \
        let c = X.new(); \
        print(c); \
    ");

    // exec("\
        struct X { \
            i32 test; \
            u8 abc; \
        } \
        X v = X { test: 10, abc: 3 }; \
        print(add(v.test, v.abc)); \
        let zero_test = [](X x) => { x.test = 0; }; \
        v.zero_test() \
        \
        var a = 2; \
        print(a); \
        a = add(a, 10); \
        print(a); \
        var b = [](var x) => { \
            x = add(x, 5); \
            print(x); \
        }; \
        b(a); \
        i32 c = b; \
    ");
    return 0;
}

struct ExecutionContextTypeInfo 
{
    uint8_t native;
    struct ExecutionContextStructDefinition* complex;
};

struct ExecutionContextStructFieldDefinition
{
    char name[MAX_IDENTIFIER_LENGTH];
    uint8_t flags; // optional?
    struct ExecutionContextTypeInfo type;
    int offset;
};

struct ExecutionContextStructFunctionDefintion
{
    int code_position;
    int parameters_count;
    struct ExecutionContextStructFieldDefinition parameters[8];
};

struct ExecutionContextStructDefinitionFieldList
{
    struct ExecutionContextStructFieldDefinition data[8];
    int count;
    int capacity;
};

void context_struct_definition_field_list_add(
    struct ExecutionContextStructDefinitionFieldList* list, 
    struct ExecutionContextStructFieldDefinition def
) {
    list->data[list->count] = def;
    list->count++;
}

struct ExecutionContextStructDefinition
{
    struct ExecutionContextStructDefinitionFieldList fields;
    struct ExecutionContextStructDefinitionFieldList static_fields;
     // 0x1: treat as tuple
     // 0x2: info about if this type needs a destructor (contains any object that needs to be dereferenced)
    uint8_t flags; 
    int size;
    int static_size;
    uint8_t* static_data;
};


#pragma region --- CONTEXT ---
struct ExecutionContextVariable
{
    char name[MAX_IDENTIFIER_LENGTH];
    int stack_index;
};

struct ExecutionContextScope
{
    struct ExecutionContextVariable variables[16];
    int variable_count;
};

struct ExecutionContextStackValue
{
    uint8_t type;
    int size;
    uint64_t* ptr;
};

struct ExecutionContext
{
    const char* code;
    int code_len;
    int position;
    struct ExecutionContextScope global_scope;
    struct ExecutionContextScope scopes[16];
    int scope_index;
    uint64_t stack[64];
    uint8_t stack_type[64];
    int stack_index;
    int stack_variables;
};

enum ExectionContextIdentifierResultType
{
    EXECUTION_CONTEXT_IDENTIFIER_RESULT_HANDLED,
    EXECUTION_CONTEXT_IDENTIFIER_RESULT_TYPE,
    EXECUTION_CONTEXT_IDENTIFIER_RESULT_VARIABLE,
    EXECUTION_CONTEXT_IDENTIFIER_RESULT_ERROR,
};

struct ExectionContextIdentifierResult 
{
    uint8_t data_type;
    union {
        struct ExecutionContextTypeInfo type_data;
        struct ExecutionContextVariable* variable_data;
    };
};

struct ExecutionContextStackIterator
{
    int stack_index;
};

int context_eof(struct ExecutionContext* context)
{
    return context->position >= context->code_len;    
}

void context_skip_spaces(struct ExecutionContext* context)
{
    const char* current = &context->code[context->position];
    const char* eof = context->code + context->code_len;

    while (isspace(*current) && current < eof) 
    { 
        ++current; 
        context->position++;
    };
}

bool check_type_is_assignable_to(uint8_t current_type, uint8_t new_type)
{
    if (!(current_type & STACK_TYPE_DYNAMIC) && !(current_type == STACK_TYPE_ACQUIRE))
    {
        if (current_type != new_type)
        {
            return false;
        }
    }

    return true;
}

int get_size_of_native_type(uint8_t type)
{
    if (type == STACK_TYPE_ACQUIRE)
    {
        return -1;
    }
    else if (type & STACK_TYPE_DYNAMIC)
    {
        return 8;
    }
    else if (type >= STACK_TYPE_NATIVE_FUNCTION && type <= NATIVE_TYPE_NATIVE_FUNCTION)
    {
        return sizeof(void*);
    }
    else if (type >= NATIVE_TYPE_I8 && type <= NATIVE_TYPE_U8) 
    {
        return 1;
    }
    else if (type >= NATIVE_TYPE_I16 && type <= NATIVE_TYPE_U16)
    {
        return 2;
    }
    else if (type >= STACK_TYPE_FUNCTION && type <= NATIVE_TYPE_FUNCTION)
    {
        return 4;
    }
    else if (type >= NATIVE_TYPE_I64 && type <= NATIVE_TYPE_DOUBLE)
    {
        return 8;
    }

    return 0;
}

int get_size_of_type(struct ExecutionContextTypeInfo type_info)
{
    if (type_info.native == STACK_TYPE_STRUCT_INSTANCE)
    {   
        if (!type_info.complex)
        {
            printf("ERR!: Complex type is NULL\n");
            return -1;
        }

        return type_info.complex->size;
    }

    return get_size_of_native_type(type_info.native);
}

void destruct_struct(struct ExecutionContextStructDefinition* definition, uint8_t* data);

void destruct_field_list(struct ExecutionContextStructDefinitionFieldList* fields, uint8_t* data)
{
    for (int i = 0; i < fields->count; i++)
    {
        struct ExecutionContextStructFieldDefinition* field_definition = &fields->data[i];

        if (field_definition->type.native == STACK_TYPE_OBJECT || field_definition->type.native == STACK_TYPE_STRUCT)
        {
            object_deref(*(void**)&data[field_definition->offset]);
        }
        else if (field_definition->type.native == STACK_TYPE_STRUCT_INSTANCE && field_definition->type.complex) {
            destruct_struct(field_definition->type.complex, &data[field_definition->offset]);
        }
    }
}

void destruct_struct(struct ExecutionContextStructDefinition* definition, uint8_t* data)
{
    if ((definition->flags & 0x2) == 0)
    {
        // No need to destruct, there is no references inside
        return;
    }

    destruct_field_list(&definition->fields, data);
}

#pragma region --- CONTEXT STACK ---

struct ExecutionContextStackValue context_stack_get_value_at_index(struct ExecutionContext* context, int index)
{
    int len = 1;

    if (context->stack_type[index] == STACK_TYPE_STRUCT_INSTANCE)
    {
        int len_index = index + 1;

        while (context->stack_type[len_index] != STACK_TYPE_STRUCT_END)
        {
            ++len_index;
        }     

        len = len_index - index;  
    }

    if (context->stack_type[index] == STACK_TYPE_STRUCT_END)
    {
        int len_index = index - 1;

        while (context->stack_type[len_index] != STACK_TYPE_STRUCT_INSTANCE)
        {
            --len_index;
        }     

        len = index - len_index;
        index = len_index;  
    }

    return (struct ExecutionContextStackValue) { 
        .type = context->stack_type[index], 
        .size = len,
        .ptr = &context->stack[index] 
    };
}

void context_stack_reset_value_at_index(struct ExecutionContext* context, int index, struct ExecutionContextStackValue value)
{
    // method is unsafe, does not care about existing data and references, 
    // should be only used to initialize or force override stack data

    if (value.type == STACK_TYPE_STRUCT || value.type == STACK_TYPE_OBJECT)
    {
        // for struct and object value will contain and address to a reference
        // counted object
        object_ref(*(void**)value.ptr);
    }
    
    if (value.type == STACK_TYPE_STRUCT_INSTANCE)
    {
        // for struct instance, first field will always be its type and it needs to be 
        // reference counted after adding to the stack
        object_ref(*(void**)value.ptr);

        memcpy(&context->stack[index], value.ptr, value.size);
        context->stack_type[index] = value.type;

        int stack_size = (value.size - 1) / 8 + 1;

        for (int i = 1; i < stack_size; i++)
        {
            context->stack_type[index + i] = value.type;
        }
    }
    else
    {
        // primitive types does not require additional logic
        context->stack[index] = *value.ptr;
        context->stack_type[index] = value.type;
    }
}

struct ExecutionContextStackValue context_stack_unset_value_at_index(struct ExecutionContext* context, int index)
{
    // index should always point at the beggining of the struct

    struct ExecutionContextStackValue current_value = context_stack_get_value_at_index(context, index);

    if (current_value.type == STACK_TYPE_STRUCT || current_value.type == STACK_TYPE_OBJECT)
    {
        object_deref(*(void**)current_value.ptr);
    }
    else if (current_value.type == STACK_TYPE_STRUCT_INSTANCE)
    {
        destruct_struct(*(void**)current_value.ptr, ((uint8_t*)current_value.ptr) + sizeof(void*));
    }

    return current_value;
}

void context_stack_set_value_at_index(struct ExecutionContext* context, int index, struct ExecutionContextStackValue value)
{
    // index should always point at the beggining of the struct

    struct ExecutionContextStackValue current_value = context_stack_unset_value_at_index(context, index);

    if (!check_type_is_assignable_to(current_value.type, value.type))
    {
        printf("ERR!: Cannot assign to variable, types are incorrect (to: %s, from: %s)\n", get_stack_type_name(current_value.type), get_stack_type_name(value.type));
        return;
    }

    context_stack_reset_value_at_index(context, index, value);
}

int context_stack_push_value(struct ExecutionContext* context, struct ExecutionContextStackValue value)
{
    if (value.size <= 0)
    {
        return -1;
    }

    int stack_size = (value.size - 1) / 8 + 1;
    int index = context->stack_index;
    context->stack_index += stack_size;

    context_stack_reset_value_at_index(context, index, value);

    #ifdef TOKEN_DEBUG
        int start_stack_index = context->stack_index;
        printf("Pushed to stack (count: %d, value: %d, value_ptr: %p, type: %d)\n", context->stack_index, context->stack[index], context->stack[index], context->stack_type[index]);
    #endif

    return index;
}

int context_stack_pop_value(struct ExecutionContext* context)
{
    if (context->stack_index == 0) 
    {
        printf("ERR!: Stack underflow\n");
        return 0;
    }

    struct ExecutionContextStackValue current_value = context_stack_unset_value_at_index(context, context->stack_index - 1);

    context->stack_index -= current_value.size;

    #ifdef TOKEN_DEBUG
        printf("Popped from stack (type: %s, size: %d, stack_index: %d)\n", stack_type_names[current_value.type], current_value.size, context->stack_index);
    #endif

    return context->stack_index;
}

struct ExecutionContextStackIterator context_stack_iterate(struct ExecutionContext* context)
{
    return (struct ExecutionContextStackIterator) {
        .stack_index = context->stack_index - 1
    };
}

struct ExecutionContextStackValue context_stack_iterator_next(struct ExecutionContext* context, struct ExecutionContextStackIterator* iterator)
{
    struct ExecutionContextStackValue value = context_stack_get_value_at_index(context, iterator->stack_index);

    iterator->stack_index -= value.size;

    return value;
}


#pragma endregion --- CONTEXT STACK ---

#pragma region --- CONTEXT SCOPE ---

void context_scope_init(struct ExecutionContext* context)
{
    context->scopes[context->scope_index].variable_count = 0;
}

struct ExecutionContextScope* context_get_scope(struct ExecutionContext* context)
{
    return &context->scopes[context->scope_index];
}

struct ExecutionContextScope* context_push_scope(struct ExecutionContext* context)
{
    context->scope_index++;
    context_scope_init(context);
    return context_get_scope(context);
}

struct ExecutionContextScope* context_pop_scope(struct ExecutionContext* context)
{
    context->scope_index--;
    return context_get_scope(context);
}

int context_scope_variables_binary_search(struct ExecutionContextScope* scope, int l, int r, const char* x)
{
    if (r >= l) {
        int mid = l + (r - l) / 2;
        const char* name = scope->variables[mid].name;

        if (strcmp(name, x) == 0)
            return mid;
 
        if (strcmp(name, x) < 0)
            return context_scope_variables_binary_search(scope, l, mid - 1, x);
 
        return context_scope_variables_binary_search(scope, mid + 1, r, x);
    }
 
    return -1;
}

int context_scope_variables_add_sorted(struct ExecutionContextScope* scope, int n, const char* x, int capacity)
{
    if (n >= capacity)
        return n;
 
    int i;
    for (i = n - 1; (i >= 0 && strcmp(scope->variables[i].name, x) > 0); i--)
        scope->variables[i + 1] = scope->variables[i];
 
    strncpy(scope->variables[i + 1].name, x, MAX_IDENTIFIER_LENGTH);
 
    return (n + 1);
}

int context_scope_variables_linear_search(struct ExecutionContextScope* scope, const char* x)
{
    int l = scope->variable_count;

    for (int i = 0; i < l; i++)
    {
        if (strcmp(scope->variables[i].name, x) == 0)
        {
            return i;
        }
    }

    return -1;
}

struct ExecutionContextVariable* context_scope_add_variable(
    struct ExecutionContextScope* scope, 
    const char* name, 
    int stack_index
) {
    int index = scope->variable_count++;
    strncpy(scope->variables[index].name, name, MAX_IDENTIFIER_LENGTH);
    scope->variables[index].stack_index = stack_index;

    #ifdef TOKEN_DEBUG
        printf("Adding variable '%s' (stack index: %d) to scope.\n", name, stack_index);
    #endif

    return &scope->variables[index];
}

#pragma endregion --- CONTEXT SCOPE ---

void context_variable_set_value(struct ExecutionContext* context, struct ExecutionContextVariable* info, struct ExecutionContextStackValue value)
{
    context_stack_set_value_at_index(context, info->stack_index, value);
}

struct ExecutionContextStackValue context_variable_get_value(struct ExecutionContext* context, struct ExecutionContextVariable* variable)
{
    return context_stack_get_value_at_index(context, variable->stack_index);
}

void context_variable_push_into_stack(struct ExecutionContext* context, struct ExecutionContextVariable* variable)
{
    context_stack_push_value(context, context_variable_get_value(context, variable));
}

struct ExecutionContextVariable* context_add_variable(
    struct ExecutionContext* context, 
    struct ExecutionContextScope* scope, 
    const char* name, 
    uint8_t declaration_type, 
    size_t size_in_bytes, 
    bool override
) { 
    if (size_in_bytes == 0)
    {
        printf("ERR!: Tried to add variable %s with size 0.\n", name);
        return NULL;
    }

    int stack_index = context->stack_index;

    if (!override)
    {
        context->stack[stack_index] = 0;
    }
    else 
    {
        if (!check_type_is_assignable_to(declaration_type, context->stack_type[stack_index]))
        {
            printf("ERR!: Cannot add variable overriding stack, types are incorrect (to: %s, from: %s)\n", get_stack_type_name(declaration_type), get_stack_type_name(context->stack_type[stack_index]));
            return NULL;
        }
    }

    context->stack_type[stack_index] = declaration_type;

    context->stack_index += (size_in_bytes - 1) / 8 + 1;
    ++context->stack_variables;

    return context_scope_add_variable(scope, name, stack_index);
}

struct ExecutionContextVariable* context_add_local_variable(
    struct ExecutionContext* context, 
    const char* name, 
    uint8_t declaration_type, 
    size_t size_in_bytes
) {
    return context_add_variable(context, context_get_scope(context), name, declaration_type, size_in_bytes, false);
}

struct ExecutionContextVariable* context_add_global_variable(
    struct ExecutionContext* context, 
    const char* name, 
    uint8_t declaration_type, 
    size_t size_in_bytes
) {
    return context_add_variable(context, &context->global_scope, name, declaration_type, size_in_bytes, false);
}

struct ExecutionContextVariable* context_lookup_variable(struct ExecutionContext* context, const char* name)
{
    #ifdef TOKEN_DEBUG
        printf("Lookup variable named '%s'.\n", name);
    #endif

    struct ExecutionContextScope* local_scope = context_get_scope(context);

    // int lookup = context_scope_variables_binary_search(scope, 0, scope->variable_count, name);
    int lookup = context_scope_variables_linear_search(local_scope, name);

    if (lookup < 0)
    {
        lookup = context_scope_variables_linear_search(&context->global_scope, name);

        if (lookup >= 0) 
        {
            return &context->global_scope.variables[lookup];
        }

        #ifdef TOKEN_DEBUG
            printf("Variable named '%s' does not exists in scope.\n", name);
        #endif
    
        return NULL;
    }

    #ifdef TOKEN_DEBUG
        printf("Found variable named '%s' (index: %d).\n", name, lookup);
    #endif

    return &local_scope->variables[lookup];
}

struct ExecutionContextTypeInfo context_get_type_from_identifier(
    struct ExecutionContext* context,
    const char* identifier, 
    int identifier_length
) {
    struct ExecutionContextTypeInfo type_info;
    type_info.native = 255;
    type_info.complex = NULL;

    if (identifier_length == 3 && strncmp(identifier, "var", 3) == 0)
    {
        // 8th bit is specifying if variable has dynamic type
        type_info.native = STACK_TYPE_DYNAMIC;
    }
    else if (identifier_length == 3 && strncmp(identifier, "let", 3) == 0)
    {
        type_info.native = STACK_TYPE_ACQUIRE;
    }
    else if (identifier_length == 3 && strncmp(identifier, "i32", 3) == 0)
    {
        type_info.native = NATIVE_TYPE_I32;
    }
    else if (identifier_length == 3 && strncmp(identifier, "u32", 3) == 0)
    {
        type_info.native = NATIVE_TYPE_U32;
    }
    else if (identifier_length == 3 && strncmp(identifier, "f32", 3) == 0)
    {
        type_info.native = NATIVE_TYPE_FLOAT;
    }
    else if (identifier_length == 3 && strncmp(identifier, "f64", 3) == 0)
    {
        type_info.native = NATIVE_TYPE_DOUBLE;
    }
    else if (identifier_length == 3 && strncmp(identifier, "i16", 3) == 0)
    {
        type_info.native = NATIVE_TYPE_I16;
    }
    else if (identifier_length == 3 && strncmp(identifier, "u16", 3) == 0)
    {
        type_info.native = NATIVE_TYPE_U16;
    }
    else if (identifier_length == 2 && strncmp(identifier, "i8", 3) == 0)
    {
        type_info.native = NATIVE_TYPE_I8;
    }
    else if (identifier_length == 2 && strncmp(identifier, "u8", 3) == 0)
    {
        type_info.native = NATIVE_TYPE_U8;
    }
    else if (identifier_length == 3 && strncmp(identifier, "i64", 3) == 0)
    {
        type_info.native = NATIVE_TYPE_I64;
    }
    else if (identifier_length == 3 && strncmp(identifier, "u64", 3) == 0)
    {
        type_info.native = NATIVE_TYPE_U64;
    }
    else 
    {
        struct ExecutionContextVariable* variable = context_lookup_variable(context, identifier);

        if (variable) 
        {
            struct ExecutionContextStackValue stack_value = context_stack_get_value_at_index(context, variable->stack_index);

            if (stack_value.type == STACK_TYPE_STRUCT)
            {
                type_info.native = STACK_TYPE_STRUCT_INSTANCE;
                type_info.complex = *(struct ExecutionContextStructDefinition**)stack_value.ptr;
                context_variable_push_into_stack(context, variable);
            }
        }
    }

    return type_info;
}

#pragma endregion --- CONTEXT ---


#pragma region --- PARSER ---

int parse_identifier(struct ExecutionContext* context, char* buffer, int max_len)
{
    const char* source = &context->code[context->position];
    char* destination = buffer;
    char* destination_end = destination + max_len;

    while (
        (isalpha(*source) || isdigit(*source)) 
        && !context_eof(context) 
        && destination < destination_end - 1
    ) {
        *destination = *source;
        context->position++;
        destination++;
        source++;
    }

    *destination = 0;
    return destination - buffer;
}

#pragma endregion --- PARSER ---

void exec_expression(struct ExecutionContext* context);
void exec_block(struct ExecutionContext* context);

void exec_number(struct ExecutionContext* context)
{
    char number[32];
    char flags = 0;
    int index = 0;
    char current = context->code[context->position];

    while ((isdigit(current) || current == '.') && context->position < context->code_len)
    {
        number[index] = current;

        if (current == '.')
        {
            flags |= 0x1;            
        }

        index++;
        context->position++;
        current = context->code[context->position];
    }

    if (current == 'f') 
    {
        flags |= 0x2;
        context->position++;
        current = context->code[context->position];
    }

    if (current == 'l') 
    {
        flags |= 0x4;
        context->position++;
        current = context->code[context->position];
    }

    if (current == 'u') 
    {
        flags |= 0x8;
        context->position++;
        current = context->code[context->position];
    }

    number[index] = 0;
    
    uint64_t value;
    double double_value;
    float float_value;

    switch (flags)
    {
    case 0x0:
        value = (int32_t)atoll(number);
        break;
    case 0x1:
        double_value = atof(number);
        value = *(uint64_t*)&double_value;
        break;
    case 0x3:
        float_value = (float)atof(number);
        value = *(uint64_t*)&float_value;
        break;
    case 0x4:
        value = (int64_t)atoll(number);
        break;
    case 0x8:
        value = (uint32_t)atoll(number);
        break;
    case 0x12:
        value = (uint64_t)atoll(number);
        break;
    default:
        break;
    }

#ifdef TOKEN_DEBUG
switch (flags)
    {
    case 0x1:
    case 0x3:
        printf("Push number '%s' (flags: %d, value: %f) to stack\n", number, flags, value);
        break;
    case 0x0:
    case 0x4:
    case 0x8:
    case 0x12:
        printf("Push number '%s' (flags: %d, value: %d) to stack\n", number, flags, value);
    default:
        break;
    }
#endif

    context_stack_push_value(
        context, 
        (struct ExecutionContextStackValue) { .ptr = &value, .type = NATIVE_TYPE_I32, .size = get_size_of_native_type(NATIVE_TYPE_I32) }
    );
}

void exec_function(
    struct ExecutionContext* context, 
    struct ExecutionContextTypeInfo return_type
) {
    char current = context->code[context->position];

    if (current == '(')
    {
        context->position++;
    }

    context_skip_spaces(context);

    uint64_t code_start = context->position;

#ifdef TOKEN_DEBUG
    printf("Function declaration at: %d\n", code_start);
#endif

    current = context->code[context->position];

    while (current != ')')
    {
        context->position++;
        current = context->code[context->position];
    }

    context->position++;

    context_skip_spaces(context);

    current = context->code[context->position];

    if (current == '=')
    {
        context->position++;
    }

    current = context->code[context->position];

    if (current == '>')
    {
        context->position++;
    }

    context_skip_spaces(context);

    current = context->code[context->position];

    if (current == '{')
    {
        context->position++;

        context_skip_spaces(context);

        int nested = 1;

        while (nested > 0)
        {
            current = context->code[context->position];
            context->position++;

            if (current == '{')
            {
                nested++;
            }
            else if (current == '}')
            {
                nested--;
            } 
        }
    }
    else 
    {
        while (current != ';' && !context_eof(context))
        {
            current = context->code[context->position];
            context->position++;
        }
    }

    context_stack_push_value(
        context,
        (struct ExecutionContextStackValue) { .ptr = &code_start, .type = STACK_TYPE_FUNCTION, .size = get_size_of_native_type(STACK_TYPE_FUNCTION) }
    );
}

void exec_bound_function(
    struct ExecutionContext* context,     
    struct ExecutionContextTypeInfo return_type
) {
    char current = context->code[context->position];

    if (current == '[')
    {
        context->position++;
    }

    context_skip_spaces(context);
    
    current = context->code[context->position];

    if (current == ']')
    {
        context->position++;
    }

    context_skip_spaces(context);

    exec_function(context, return_type);
}

void exec_struct_field(struct ExecutionContext* context, struct ExecutionContextStructDefinition* definition)
{
    char type_identifier[MAX_IDENTIFIER_LENGTH];
    context_skip_spaces(context);
    int type_identifier_length = parse_identifier(context, type_identifier, MAX_IDENTIFIER_LENGTH);
    context_skip_spaces(context);

    char current = context->code[context->position];

    struct ExecutionContextTypeInfo type_info = 
        context_get_type_from_identifier(context, type_identifier, type_identifier_length);

    char identifier[MAX_IDENTIFIER_LENGTH];
    context_skip_spaces(context);
    parse_identifier(context, identifier, MAX_IDENTIFIER_LENGTH);
    context_skip_spaces(context);

    struct ExecutionContextStructFieldDefinition field_definition;
    strncpy(field_definition.name, identifier, MAX_IDENTIFIER_LENGTH);
    field_definition.flags = 0;

    if (current == '(')
    {
        // Method definition
        exec_function(context, type_info);

        field_definition.type = (struct ExecutionContextTypeInfo) { .native = STACK_TYPE_FUNCTION, .complex = NULL };
        field_definition.offset = definition->static_size;
        definition->static_size += get_size_of_type(field_definition.type);

        context_struct_definition_field_list_add(&definition->static_fields, field_definition);
    }
    else
    {
        field_definition.type = type_info;
        field_definition.offset = definition->size;
        definition->size += get_size_of_type(type_info);

        context_struct_definition_field_list_add(&definition->fields, field_definition);
    }
}

void exec_struct(struct ExecutionContext* context)
{
    context_skip_spaces(context);

    char current = context->code[context->position];

    if (current != '{')
    {
        char identifier[MAX_IDENTIFIER_LENGTH];
        int identifier_length = parse_identifier(context, identifier, MAX_IDENTIFIER_LENGTH);
    }

    context_skip_spaces(context);
    current = context->code[context->position];

    struct ExecutionContextStructDefinition* definition = object_create(sizeof(struct ExecutionContextStructDefinition));
    definition->flags = 0;
    definition->size = 0;
    definition->static_size = 0;
    definition->fields.capacity = 8;
    definition->fields.count = 0;
    definition->static_fields.capacity = 8;
    definition->static_fields.count = 0;

    if (current != '{')
    {
        printf("ERR!: Epected '{'\n");
        return;
    }

    context->position++;
    current = context->code[context->position];

    if (current != '}')
    {
        int start_stack_pointer = context->stack_index;

        // Parse fields
        do
        {
            if (current == ';')
            {
                context->position++;
                context_skip_spaces(context);
                current = context->code[context->position];
            }

            if (current == '}')
            {
                break;
            }

            exec_struct_field(context, definition);

            context_skip_spaces(context);
            current = context->code[context->position];
        }
        while (current == ';');

        definition->static_data = malloc(definition->static_size);
        memcpy(definition->static_data, &context->stack[start_stack_pointer], definition->static_size);
        context->stack_index = start_stack_pointer;

        if (current == '}')
        {
            context->position++;
        }
        else 
        {
            printf("ERR!: Syntax error missing ')'\n");
            // FIXME: should panic
            return;
        }
    }
    else 
    {
        context->position++;
    }

    uint64_t value = (uint64_t)definition;

    context_stack_push_value(
        context, 
        (struct ExecutionContextStackValue) { .ptr = &value, .type = STACK_TYPE_STRUCT, .size = get_size_of_native_type(STACK_TYPE_STRUCT) }
    );
}

void exec_assignment(struct ExecutionContext* context, struct ExecutionContextVariable* variable) 
{
    struct ExecutionContextStackValue value = context_stack_get_value_at_index(context, context->stack_index - 1);

#ifdef TOKEN_DEBUG
    // printf("Assign value '%d' to %p\n", context->stack[context->stack_index - 1], (uint64_t*)context->stack[context->stack_index - 2]);
    printf("Assign value '[%s] %d' to '%s'\n", get_stack_type_name(value.type), context->stack[context->stack_index - 1], variable->name);
#endif

    context_variable_set_value(context, variable, value);
    
    context_stack_pop_value(context);
}

void exec_variable_declaration(struct ExecutionContext* context, const char* identifier, struct ExecutionContextTypeInfo declaration_type)
{
    context_skip_spaces(context);

    char current = context->code[context->position];

    if (current != '=')
    {
        printf("ERR!: Expected '=' for '%s' variable declaration.\n", identifier);
    }
    else 
    {
        context->position++;
    }

    exec_expression(context);

    struct ExecutionContextStackValue value = context_stack_get_value_at_index(context, context->stack_index - 1);

    if (value.type == STACK_TYPE_STRUCT_INSTANCE)
    {

    }

    context->stack_index -= value.size;

    struct ExecutionContextVariable* variable = context_add_variable(
        context, 
        context_get_scope(context), 
        identifier, 
        declaration_type.native, 
        value.size * 8,
        true
    );

    if (!variable)
    {
        printf("ERR!: Cannot add local variable '%s'.\n", identifier);
        return;
    }

    context_variable_set_value(context, variable, value);

#ifdef TOKEN_DEBUG
    printf("Declared variable '%s' with value '[%s] %d'\n", variable->name, get_stack_type_name(value.type), context->stack[context->stack_index - 1]);
#endif

}

struct ExectionContextIdentifierResult exec_identifier(struct ExecutionContext* context, char* identifier, int max_len)
{
    int identifier_length = parse_identifier(context, identifier, max_len);

    if (identifier_length == 6 && strncmp(identifier, "struct", identifier_length) == 0)
    {
        exec_struct(context);
        return (struct ExectionContextIdentifierResult) { .data_type = EXECUTION_CONTEXT_IDENTIFIER_RESULT_HANDLED };
    }

    struct ExecutionContextTypeInfo type_info = 
        context_get_type_from_identifier(context, identifier, MAX_IDENTIFIER_LENGTH);

    struct ExectionContextIdentifierResult result;

    if (type_info.native == 255)
    {
        // When identifier was not a type then we should look for a variable and 
        // push it to the stack
        struct ExecutionContextVariable* variable = context_lookup_variable(context, identifier);

        result.data_type = EXECUTION_CONTEXT_IDENTIFIER_RESULT_VARIABLE;
        result.variable_data = variable;

        if (!variable)
        {
            printf("ERR!: Variable %s is not defined.\n", identifier);
            result.data_type = EXECUTION_CONTEXT_IDENTIFIER_RESULT_ERROR;
            return result;
        }

        context_variable_push_into_stack(context, variable);
    }
    else 
    {
        result.data_type = EXECUTION_CONTEXT_IDENTIFIER_RESULT_TYPE;
        result.type_data = type_info;
    }

    return result;
}

void exec_field_access(struct ExecutionContext* context)
{
    struct ExecutionContextStackValue value = context_stack_get_value_at_index(context, context->stack_index - 1);
    struct ExecutionContextStructDefinitionFieldList* fields = NULL;
    uint8_t* data;

    if (value.type == STACK_TYPE_STRUCT)
    {
        struct ExecutionContextStructDefinition* definition = *(struct ExecutionContextStructDefinition**)value.ptr;
        fields = &definition->static_fields;
        data = definition->static_data;
    }
    else if (value.type == STACK_TYPE_STRUCT_INSTANCE)
    {
        struct ExecutionContextStructDefinition* definition = *(struct ExecutionContextStructDefinition**)value.ptr;
        fields = &definition->fields;
        data = ((uint8_t*)value.ptr) + sizeof(struct ExecutionContextStructDefinition*);
    }
    else if (value.type == STACK_TYPE_OBJECT)
    {
        uint8_t* heap_data = (uint8_t*)value.ptr;
        struct ExecutionContextStructDefinition* definition = *(struct ExecutionContextStructDefinition**)heap_data;
        fields = &definition->fields;
        data = heap_data + sizeof(struct ExecutionContextStructDefinition*);
    }

    struct ExecutionContextStructFieldDefinition* field = NULL;
    uint8_t* field_data = data + field->offset;

    context_stack_push_value(
        context,
        (struct ExecutionContextStackValue) { .ptr = (uint64_t*)field_data, .type = field->type.native, .size = get_size_of_type(field->type) }
    );
}

int exec_call_args(struct ExecutionContext* context, int start_stack_index) 
{
    // Parsing an expression here, because ',' operator pushes all expressions 
    // into a stack, which means this will populate arguments for this call
    exec_expression(context);

    char current = context->code[context->position];

    // We should be at ')', if not there is something wrong with syntax
    if (current == ')')
    {
        context->position++;
    }
    else 
    {
        printf("ERR!: Syntax error missing ')'\n");
        // FIXME: should panic
        return 0;
    }

    // Provided args size pushed to the stack can be calculated
    int args_stack_size = context->stack_index - start_stack_index;

    return args_stack_size;
}

void exec_call_cleanup(struct ExecutionContext* context, int frame_start_stack_index, int args_stack_size) 
{
    int return_count = context->stack_index - (frame_start_stack_index + args_stack_size);
    int to_pop = args_stack_size - return_count + 1;

    for (int i = 0; i < return_count; i++)
    {
        #ifdef TOKEN_DEBUG
            printf("Returned value %d\n", context->stack[context->stack_index - i - 1]);
        #endif

        context_stack_set_value_at_index(context, frame_start_stack_index + i, context_stack_get_value_at_index(context, context->stack_index - i - 1));
    }

    for (int i = 0; i < to_pop; i++)
    {
        context_stack_pop_value(context);
    }
}

void exec_call_native_function(struct ExecutionContextStackValue stack_value, struct ExecutionContext* context)
{
#ifdef TOKEN_DEBUG
    printf("Calling function at address: %p\n", *stack_value.ptr);
#endif

    if (stack_value.type != STACK_TYPE_NATIVE_FUNCTION)
    {
        printf("ERR!: Value is not a function\n");
        return;
    }

    char current = context->code[context->position];

    void(*func)(struct ExecutionContext*) = *(void**)stack_value.ptr;
    int frame_start_stack_index = context->stack_index;

    #ifdef TOKEN_DEBUG
        printf("Prepare to call %p\n", func);
    #endif

    int args_count = exec_call_args(context, frame_start_stack_index);

    #ifdef TOKEN_DEBUG
        printf("Calling %p with %d arguments\n", func, args_count);
    #endif

    func(context);

    exec_call_cleanup(context, frame_start_stack_index, args_count);
}

void exec_call_function(struct ExecutionContextStackValue stack_value, struct ExecutionContext* context)
{
#ifdef TOKEN_DEBUG
    printf("Calling function at position: %d\n", *stack_value.ptr);
#endif

    if (stack_value.type != STACK_TYPE_FUNCTION)
    {
        printf("ERR!: Value is not a function\n");
        return;
    }

    // Stack value contains start text position for the function, currently
    // it should point to place directly after '(' character
    int func_position = *stack_value.ptr;
    // Save start stack index for later, we need to restore it after
    // function call is done 
    int frame_start_stack_index = context->stack_index;

    #ifdef TOKEN_DEBUG
        printf("Prepare to call %p\n", func_position);
    #endif

    int args_stack_size = exec_call_args(context, frame_start_stack_index);

    int return_position = context->position;

    context->position = func_position;
    context_skip_spaces(context);
    char current = context->code[context->position];

    struct ExecutionContextScope* scope = context_push_scope(context);

    if (current != ')')
    {
        // Args on the stack needs to be assinged to scope variables
        int index = 0;
        char type_identifier[MAX_IDENTIFIER_LENGTH];
        char identifier[MAX_IDENTIFIER_LENGTH];

        // Move stack back to frame start, so we can assign pushed values to variables
        context->stack_index = frame_start_stack_index;

        while (index < args_stack_size)
        {
            // Read the type of the variable
            context_skip_spaces(context);
            int type_identifier_length = parse_identifier(context, type_identifier, MAX_IDENTIFIER_LENGTH);
            context_skip_spaces(context);

            struct ExecutionContextTypeInfo type_info = context_get_type_from_identifier(context, type_identifier, type_identifier_length);

            // Read the name of the variable
            context_skip_spaces(context);
            parse_identifier(context, identifier, MAX_IDENTIFIER_LENGTH);
            context_skip_spaces(context);

            // Get pushed value at current index which will be assigned to variable
            struct ExecutionContextStackValue arg_stack_value = context_stack_get_value_at_index(context, frame_start_stack_index + index);
            index += arg_stack_value.size;

            // Override values on stack so we create new variables without extra value copy,
            // this moves stack pointer automatically
            struct ExecutionContextVariable* variable = context_add_variable(
                context, 
                scope, 
                identifier, 
                type_info.native, 
                arg_stack_value.size, 
                true
            );

            current = context->code[context->position];

            if (current != ',')
            {
                break;
            }

            context->position++;
        }

        if (current == ')')
        {
            context->position++;
        }
        else 
        {
            printf("ERR!: Syntax error missing ')'\n");
            // FIXME: should panic
            return;
        }
    }
    else 
    {
        context->position++;
    }

    context_skip_spaces(context);

    current = context->code[context->position];

    if (current == '=')
    {
        context->position++;
    }

    current = context->code[context->position];

    if (current == '>')
    {
        context->position++;
    }

    exec_expression(context);

    context->position = return_position;
    
    exec_call_cleanup(context, frame_start_stack_index, args_stack_size);
    context_pop_scope(context);
}

void exec_call(struct ExecutionContext* context) 
{
    struct ExecutionContextStackValue stack_value = context_stack_get_value_at_index(context, context->stack_index - 1);

    if (stack_value.type == STACK_TYPE_NATIVE_FUNCTION)
    {
        exec_call_native_function(stack_value, context);
    }
    else if (stack_value.type == STACK_TYPE_FUNCTION)
    {
        exec_call_function(stack_value, context);
    }
    else 
    {
        printf("ERR!: Value is not a function\n");
    }
}

void exec_expression(struct ExecutionContext* context)
{
    char current;
    char last_expression = 0;

    struct ExectionContextIdentifierResult last_identifier_result;

    #ifdef TOKEN_DEBUG
        printf("Parsing expression\n");
    #endif

    while (context->position < context->code_len)
    {
        context_skip_spaces(context);
        current = context->code[context->position];

        #ifdef TOKEN_DEBUG
            printf("TOKEN: %c\n", current);
        #endif

        if (isalpha(current)) 
        {
            char identifier[MAX_IDENTIFIER_LENGTH];

            if (last_identifier_result.data_type == EXECUTION_CONTEXT_IDENTIFIER_RESULT_TYPE)
            {
                // Last expression was identifier representing a type, so this is a variable declaration
                context_skip_spaces(context);

                int identifier_length = parse_identifier(context, identifier, MAX_IDENTIFIER_LENGTH);

                struct ExecutionContextVariable* variable = context_lookup_variable(context, identifier);
                
                if (variable)
                {
                    printf("ERR!: Variable %s is aready defined.\n", identifier);
                    return;
                }

                exec_variable_declaration(context, identifier, last_identifier_result.type_data);
                continue;
            }        

            last_identifier_result = exec_identifier(context, identifier, MAX_IDENTIFIER_LENGTH);

            if (last_identifier_result.data_type >= EXECUTION_CONTEXT_IDENTIFIER_RESULT_ERROR)
            {
                printf("ERR!: Identifier %s is not known identifier\n", identifier);
                return;
            }

            continue;
        }
        else if (isdigit(current) || current == '.')
        {
            if (current == '.')
            {
                struct ExecutionContextStackValue value = context_stack_get_value_at_index(context, context->stack_index - 1);

                if (value.type == STACK_TYPE_STRUCT || value.type == STACK_TYPE_STRUCT_INSTANCE || value.type == STACK_TYPE_OBJECT)
                {
                    exec_field_access(context);
                }
                else 
                {
                    exec_number(context);
                }
            }
            else
            {
                // 1 -> int32_t
                // 1.0 -> double
                // 1.0f -> float
                // 1u -> uint32_t
                // 1l -> int64_t
                // 1lu -> uint64_t
                exec_number(context);
            }
        }
        else if (current == '=')
        {
            if (last_identifier_result.data_type == EXECUTION_CONTEXT_IDENTIFIER_RESULT_VARIABLE)
            {
                // Last identifier was variable, so this is variable assignment
                context->position++;
                exec_expression(context);
                exec_assignment(context, last_identifier_result.variable_data);
            }
        }
        else if (current == ';')
        {
            break;
        }
        else if (current == ',')
        {
            context->position++;
        }
        else if (current == '(')
        {
            struct ExecutionContextStackValue value = context_stack_get_value_at_index(context, context->stack_index - 1);

            if (last_identifier_result.data_type == EXECUTION_CONTEXT_IDENTIFIER_RESULT_TYPE)
            {
                // Last expression was identifier representing a type, so this is now
                // a function declaration
                exec_function(
                    context, 
                    last_identifier_result.type_data 
                );
            }
            else if (last_identifier_result.data_type == EXECUTION_CONTEXT_IDENTIFIER_RESULT_VARIABLE)
            {
                // Last identifier was a variable so this is a call expression
                context->position++;
                exec_call(context);
            } 
        }
        else if (current == ')')
        {
            break;
        }
        else if (current == '/' && context->position + 1 < context->code_len && context->code[context->position + 1] == '/')
        {
            while(context->position < context->code_len && current != '\n')
            {
                context->position++;
                current = context->code[context->position];
            }

            context->position++;
        }
        else if (current == '[')
        {
            if (last_identifier_result.data_type == EXECUTION_CONTEXT_IDENTIFIER_RESULT_TYPE)
            {
                // Last expression was identifier representing a type, so this is now
                // a bound function declaration
                exec_bound_function(
                    context, 
                    last_identifier_result.type_data
                );
            }
        }
        else if (current == '{')
        {
            context->position++;
            exec_block(context);
        }
        else 
        {
            printf("unknown char %c\n", current);            
            context->position++;
        }
        
        last_identifier_result.data_type = EXECUTION_CONTEXT_IDENTIFIER_RESULT_HANDLED;
    }

    #ifdef TOKEN_DEBUG
        printf("End parsing expression\n");
    #endif
}

void exec_block(struct ExecutionContext* context)
{
    char current;

    #ifdef TOKEN_DEBUG
        printf("Parsing block\n");
    #endif

    int block_stack_index = context->stack_index;
    int block_stack_variables = context->stack_variables;

    while (context->position < context->code_len)
    {
        int stack_index = context->stack_index;

#ifdef TOKEN_DEBUG
        printf("--- Parsing statement (stack_index: %d) ---\n", stack_index);
#endif

#ifdef TEST_DEBUG
        // getchar();
#endif

        exec_expression(context);

        context_skip_spaces(context);
        current = context->code[context->position];

        if (current == ';')
        {
            context->position++;
        }

        context_skip_spaces(context);
        current = context->code[context->position];

        #ifdef TOKEN_DEBUG
            printf("--- End parsing statement (stack_index: %d) ---\n", context->stack_index);
        #endif

        context->stack_index = stack_index >= context->stack_variables ? stack_index : context->stack_variables;

        if (current == '}')
        {
            context->position++;
            break;
        }
    }

    context->stack_index = block_stack_index;
    context->stack_variables = block_stack_variables;

    #ifdef TOKEN_DEBUG
        printf("End parsing block\n");
    #endif
}

#pragma region --- SCRIPT FUNCTIONS ---

void fts_print(struct ExecutionContext* context)
{
    struct ExecutionContextStackIterator iterator = context_stack_iterate(context);
    struct ExecutionContextStackValue value = context_stack_iterator_next(context, &iterator);

    if (value.type == NATIVE_TYPE_I32) 
    {
        printf("%d\n", context->stack[context->stack_index - 1]);
    }
    else 
    {
        printf("Invalid type\n");
    }
}

void fts_add(struct ExecutionContext* context)
{
    struct ExecutionContextStackIterator iterator = context_stack_iterate(context);
    
    // second parameter
    struct ExecutionContextStackValue value2 = context_stack_iterator_next(context, &iterator);
    // first parameter
    struct ExecutionContextStackValue value = context_stack_iterator_next(context, &iterator);

    if (value.type == NATIVE_TYPE_I32 && value2.type == NATIVE_TYPE_I32) 
    {
        int32_t result = *(int32_t*)value.ptr + *(int32_t*)value2.ptr;
        uint64_t value = result;

        context_stack_push_value(
            context, 
            (struct ExecutionContextStackValue) { .ptr = &value, .type = NATIVE_TYPE_I32, .size = get_size_of_native_type(NATIVE_TYPE_I32) }
        );
    }
}

#pragma endregion --- SCRIPT FUNCTIONS ---

void exec(const char* code)
{
    struct ExecutionContext context;
    context.code = code;
    context.code_len = strlen(code);
    context.scope_index = 0;
    context.position = 0;
    context.stack_index = 0;
    context.stack_variables = 0;

    context.global_scope.variable_count = 0;
    context_scope_init(&context);

    uint64_t fts_print_value = (uint64_t)&fts_print;

    context_variable_set_value(
        &context, 
        context_add_global_variable(&context, "print", STACK_TYPE_NATIVE_FUNCTION, 1), 
        (struct ExecutionContextStackValue) { .ptr = &fts_print_value, .type = STACK_TYPE_NATIVE_FUNCTION, .size = get_size_of_native_type(STACK_TYPE_NATIVE_FUNCTION) }
    );
    
    uint64_t fts_add_value = (uint64_t)&fts_add;

    context_variable_set_value(
        &context, 
        context_add_global_variable(&context, "add", STACK_TYPE_NATIVE_FUNCTION, 1), 
        (struct ExecutionContextStackValue) { .ptr = &fts_add_value, .type = STACK_TYPE_NATIVE_FUNCTION, .size = get_size_of_native_type(STACK_TYPE_NATIVE_FUNCTION) }
    );

    exec_block(&context);
}