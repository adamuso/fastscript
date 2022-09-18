#include "context.h"

#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "defs.h"
#include "object.h"
#include "debug.h"

#pragma region --- CONTEXT ---

void context_struct_definition_field_list_add(
    struct ExecutionContextStructDefinitionFieldList* list, 
    struct ExecutionContextStructFieldDefinition def
) {
    list->data[list->count] = def;
    list->count++;
}

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
    else if (type >= STACK_TYPE_STRUCT && type <= NATIVE_TYPE_NATIVE_FUNCTION)
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
    else if (type >= NATIVE_TYPE_U32 && type <= NATIVE_TYPE_FUNCTION)
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
            debug("ERR!: Complex type is NULL\n");
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

struct ExecutionContextStackValue context_stack_get_last_value(struct ExecutionContext* context)
{
    return context_stack_get_value_at_index(context, context->stack_index - 1);
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
        debug("ERR!: Cannot assign to variable, types are incorrect (to: %s, from: %s)\n", get_stack_type_name(current_value.type), get_stack_type_name(value.type));
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
        debug("Pushed to stack (count: %d, value: %d, value_ptr: %p, type: %d)\n", context->stack_index, context->stack[index], context->stack[index], context->stack_type[index]);
    #endif

    return index;
}

int context_stack_pop_value(struct ExecutionContext* context)
{
    if (context->stack_index == 0) 
    {
        debug("ERR!: Stack underflow\n");
        return 0;
    }

    struct ExecutionContextStackValue current_value = context_stack_unset_value_at_index(context, context->stack_index - 1);

    context->stack_index -= current_value.size;

    #ifdef TOKEN_DEBUG
        debug("Popped from stack (type: %s, size: %d, stack_index: %d)\n", get_stack_type_name(current_value.type), current_value.size, context->stack_index);
    #endif

    return context->stack_index;
}

struct ExecutionContextStackIterator context_stack_iterate(struct ExecutionContext* context)
{
    return (struct ExecutionContextStackIterator) {
        .stack_index = context->stack_index
    };
}

struct ExecutionContextStackValue context_stack_iterator_next(struct ExecutionContext* context, struct ExecutionContextStackIterator* iterator)
{
    struct ExecutionContextStackValue value = context_stack_get_value_at_index(context, iterator->stack_index - 1);

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
        debug("Adding variable '%s' (stack index: %d) to scope.\n", name, stack_index);
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
        debug("ERR!: Tried to add variable %s with size 0.\n", name);
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
            debug("ERR!: Cannot add variable overriding stack, types are incorrect (to: %s, from: %s)\n", get_stack_type_name(declaration_type), get_stack_type_name(context->stack_type[stack_index]));
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
        debug("Lookup variable named '%s'.\n", name);
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
            debug("Variable named '%s' does not exists in scope.\n", name);
        #endif
    
        return NULL;
    }

    #ifdef TOKEN_DEBUG
        debug("Found variable named '%s' (index: %d).\n", name, lookup);
    #endif

    return &local_scope->variables[lookup];
}

struct ExecutionContextTypeInfo context_get_type_from_identifier(
    struct ExecutionContext* context,
    const char* identifier, 
    int identifier_length,
    struct ExecutionContextVariable** found_variable
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

        *found_variable = variable;
    }

    return type_info;
}

#pragma endregion --- CONTEXT ---
