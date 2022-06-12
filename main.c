#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <stdlib.h>

#define TOKEN_DEBUG

enum {
    STACK_TYPE_INTEGER,
    STACK_TYPE_NATIVE_FUNCTION,
    STACK_TYPE_FUNCTION
};

void exec(const char* code);

int main() 
{
    exec("\
        a = 2; \
        print(a); \
        a = add(a, 10); \
        print(a); \
        // b = () => {}; \n\
    ");
    return 0;
}

//
//
//
//

struct ExecutionContextVariable
{
    char name[32];
    uint64_t value;
    uint8_t type;
};

struct ExecutionContextScope
{
    struct ExecutionContextVariable variables[16];
    int variable_count;
};

struct ExecutionContextStackValue
{
    uint64_t value;
    uint8_t type;
};

struct ExecutionContext
{
    const char* code;
    int code_len;
    int position;
    struct ExecutionContextScope scopes[16];
    int scope_index;
    uint64_t stack[32];
    uint8_t stack_type[32];
    int stack_index;
};

struct ExecutionContextVariableInfo
{
    int index;
    struct ExecutionContextVariable* variable;
};

struct ExecutionContextScope* context_get_scope(struct ExecutionContext* context)
{
    return &context->scopes[context->scope_index];
}

void context_skip_spaces(struct ExecutionContext* context)
{
    char* current = &context->code[context->position];
    char* eof = context->code + context->code_len;

    while (isspace(*current) && current < eof) 
    { 
        ++current; 
    };
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

int context_push_stack(struct ExecutionContext* context, uint64_t value, uint8_t type)
{
    int index = context->stack_index++;
    context->stack[index] = value;
    context->stack_type[index] = type;

    #ifdef TOKEN_DEBUG
        int start_stack_index = context->stack_index;
        printf("Pushed to stack (count: %d, value: %d, value_ptr: %p, type: %d)\n", context->stack_index, context->stack[index], context->stack[index], context->stack_type[index]);
    #endif

    return index;
}

int context_pop_stack(struct ExecutionContext* context)
{
    if (context->stack_index == 0) 
    {
        printf("ERR!: Stack underflow\n");
        return 0;
    }

    context->stack_index--;

    #ifdef TOKEN_DEBUG
        int start_stack_index = context->stack_index;
        printf("Popped from stack (count: %d)\n", context->stack_index);
    #endif

    return context->stack_index;
}

void context_stack_set_value_at_index(struct ExecutionContext* context, int index, struct ExecutionContextStackValue value)
{
    context->stack[index] = value.value;
    context->stack_type[index] = value.type;
}

struct ExecutionContextStackValue context_stack_get_value_at_index(struct ExecutionContext* context, int index)
{
    return (struct ExecutionContextStackValue) { .value = context->stack[index], .type = context->stack_type[index] };
}

void context_scope_init(struct ExecutionContext* context)
{
    context->scopes[context->scope_index].variable_count = 0;
}

struct ExecutionContextVariableInfo context_scope_add_variable(struct ExecutionContextScope* scope, const char* name)
{
    int index = scope->variable_count++;
    strncpy(scope->variables[index].name, name, 32);
    scope->variables[index].value = 0;
    scope->variables[index].type = 0;

    #ifdef TOKEN_DEBUG
        printf("Adding variable '%s' (index: %d) to scope.\n", name, index);
    #endif

    return (struct ExecutionContextVariableInfo) { .index = index, .variable = &scope->variables[index] };
}

void context_set_variable_ptr(struct ExecutionContext* context, struct ExecutionContextVariableInfo info, void* ptr, uint8_t type)
{
    info.variable->value = (uint64_t)ptr;
    info.variable->type = type;
}

struct ExecutionContextVariableInfo context_scope_lookup_variable(struct ExecutionContextScope* scope, const char* name)
{
    #ifdef TOKEN_DEBUG
        printf("Lookup variable named '%s'.\n", name);
    #endif

    int lookup = context_scope_variables_binary_search(scope, 0, scope->variable_count, name);

    if (lookup < 0)
    {
        #ifdef TOKEN_DEBUG
            printf("Variable named '%s' does not exists in scope.\n", name);
        #endif
    
        return context_scope_add_variable(scope, name);
    }

    #ifdef TOKEN_DEBUG
        printf("Found variable named '%s' (index: %d).\n", name, lookup);
    #endif

    return (struct ExecutionContextVariableInfo) { .index = lookup, .variable = &scope->variables[lookup] };
}

void exec_expression(struct ExecutionContext* context);

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

    context_push_stack(context, value, STACK_TYPE_INTEGER);
}

void exec_function(struct ExecutionContext* context)
{
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

    int code_start = context->position;

    if (current == '(')
    {
        context->position++;
    }

    context_skip_spaces(context);

    current = context->code[context->position];

    if (current == ')')
    {
        context->position++;
    }

    context_skip_spaces(context);

    current = context->code[context->position];

    if (current == '=')
    {
        context->position++;
    }

    context_skip_spaces(context);

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
    }

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

    context_push_stack(context, code_start, STACK_TYPE_FUNCTION);
}

struct ExecutionContextVariableInfo exec_identifier(struct ExecutionContext* context)
{
    char identifier[32];
    int index = 0;
    char current = context->code[context->position];

    while ((isalpha(current) || isdigit(current)) && context->position < context->code_len)
    {
        identifier[index] = current;
        index++;
        context->position++;
        current = context->code[context->position];
    }

    identifier[index] = 0;
    
    struct ExecutionContextVariableInfo variable = context_scope_lookup_variable(context_get_scope(context), identifier);

#ifdef TOKEN_DEBUG
    printf("Push variable '%s' (index: %d) to stack\n", identifier, variable.index);
#endif
    
    context_push_stack(context, variable.variable->value, variable.variable->type);
    return variable;
}

void exec_assignment(struct ExecutionContext* context, struct ExecutionContextVariable* variable) 
{
#ifdef TOKEN_DEBUG
    // printf("Assign value '%d' to %p\n", context->stack[context->stack_index - 1], (uint64_t*)context->stack[context->stack_index - 2]);
    printf("Assign value '%d' to '%s'\n", context->stack[context->stack_index - 1], variable->name);
#endif

    struct ExecutionContextStackValue value = context_stack_get_value_at_index(context, context->stack_index - 1);
    variable->value = value.value;
    variable->type = value.type;
}

void exec_call_native_function(struct ExecutionContextStackValue stack_value, struct ExecutionContext* context)
{
    if (stack_value.type != STACK_TYPE_NATIVE_FUNCTION)
    {
        printf("ERR!: Value is not a function\n");
        return;
    }

    char current = context->code[context->position];

    void(*func)(struct ExecutionContext*) = (void*)stack_value.value;
    int start_stack_index = context->stack_index;

    #ifdef TOKEN_DEBUG
        printf("Prepare to call %p\n", func);
    #endif

    // do
    // {
        exec_expression(context);
        current = context->code[context->position];

        if (current == ')')
        {
            context->position++;
        }
    // }
    // while (current == ',' && context->position < context->code_len);

    int args_count = context->stack_index - start_stack_index;

    #ifdef TOKEN_DEBUG
        printf("Calling %p with %d arguments\n", func, args_count);
    #endif

    func(context);

    int return_count = context->stack_index - (start_stack_index + args_count);
    int to_pop = args_count - return_count + 1;

    for (int i = 0; i < return_count; i++)
    {
        #ifdef TOKEN_DEBUG
            printf("Returned value %d\n", context->stack[context->stack_index - i - 1]);
        #endif

        context_stack_set_value_at_index(context, start_stack_index + i, context_stack_get_value_at_index(context, context->stack_index - i - 1));
    }

    for (int i = 0; i < to_pop; i++)
    {
        context_pop_stack(context);
    }
}

void exec_call_function(struct ExecutionContextStackValue stack_value, struct ExecutionContext* context)
{
    if (stack_value.type != STACK_TYPE_FUNCTION)
    {
        printf("ERR!: Value is not a function\n");
        return;
    }


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

    struct ExecutionContextVariableInfo last_identifier;

    #ifdef TOKEN_DEBUG
        printf("Parsing expression\n");
    #endif

    while (context->position < context->code_len)
    {
        current = context->code[context->position];

        #ifdef TOKEN_DEBUG
            printf("TOKEN: %c\n", current);
        #endif

        if (isspace(current))
        {
            context->position++;
            continue;
        }
        else if (isalpha(current)) 
        {
            last_identifier = exec_identifier(context);
            last_expression = 1;
            last_identifier.index = -1;
            continue;
        }
        else if (isdigit(current) || current == '.')
        {
            // 1 -> int32_t
            // 1.0 -> double
            // 1.0f -> float
            // 1u -> uint32_t
            // 1l -> int64_t
            // 1lu -> uint64_t
            exec_number(context);
        }
        else if (current == '=')
        {
            context->position++;
            exec_expression(context);
            exec_assignment(context, last_identifier.variable);
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
            if (last_expression == 1)
            {
                // If last expression was identifier we treat this as a call expression
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
            
        }
        else 
        {
            printf("unknown char %c\n", current);            
            context->position++;
        }
        
        last_expression = 0;
        last_identifier.index = -1;
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

    while (context->position < context->code_len)
    {
        int stack_index = context->stack_index;

        #ifdef TOKEN_DEBUG
            printf("--- Parsing statement (stack_index: %d) ---\n", stack_index);
        #endif

        exec_expression(context);
        current = context->code[context->position];

        if (current == ';')
        {
            context->position++;
        }

        #ifdef TOKEN_DEBUG
            printf("--- End parsing statement (stack_index: %d) ---\n", context->stack_index);
        #endif

        context->stack_index = stack_index;
    }

    #ifdef TOKEN_DEBUG
        printf("End parsing block\n");
    #endif
}

void fts_print(struct ExecutionContext* context)
{
    struct ExecutionContextStackValue value = context_stack_get_value_at_index(context, context->stack_index - 1);

    if (value.type == STACK_TYPE_INTEGER) 
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
    struct ExecutionContextStackValue value = context_stack_get_value_at_index(context, context->stack_index - 1);

    if (value.type == STACK_TYPE_INTEGER) 
    {
        context_push_stack(context, context->stack[context->stack_index - 1] + context->stack[context->stack_index - 2], STACK_TYPE_INTEGER);
    }
}

void exec(const char* code)
{
    struct ExecutionContext context;
    context.code = code;
    context.code_len = strlen(code);
    context.scope_index = 0;
    context.position = 0;
    context.stack_index = 0;

    context_scope_init(&context);

    context_set_variable_ptr(&context, context_scope_add_variable(context_get_scope(&context), "print"), &fts_print, STACK_TYPE_NATIVE_FUNCTION);
    context_set_variable_ptr(&context, context_scope_add_variable(context_get_scope(&context), "add"), &fts_add, STACK_TYPE_NATIVE_FUNCTION);

    exec_block(&context);
}