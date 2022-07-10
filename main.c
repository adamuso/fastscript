#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <stdlib.h>

#define TEST_DEBUG
#define TOKEN_DEBUG

enum {
    STACK_TYPE_INTEGER,
    STACK_TYPE_NATIVE_FUNCTION,
    STACK_TYPE_FUNCTION,

    // 8th bit is specifying if variable has dynamic type
    STACK_TYPE_DYNAMIC = 0x80
};

#ifdef TOKEN_DEBUG
const char* stack_type_names[3] = {
    "integer",
    "native_function",
    "function"
};
#endif

#define MAX_IDENTIFIER_LENGTH 32

void exec(const char* code);

int main() 
{
    exec("\
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




#pragma region --- CONTEXT ---
struct ExecutionContextVariable
{
    char name[MAX_IDENTIFIER_LENGTH];
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
    struct ExecutionContextScope global_scope;
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

uint8_t context_get_type_from_identifier(struct ExecutionContext* context, const char* identifier, int identifier_length) 
{
    uint8_t declaration_type = 255;

    if (identifier_length == 3 && strncmp(identifier, "var", 3) == 0)
    {
        // 8th bit is specifying if variable has dynamic type
        declaration_type = STACK_TYPE_DYNAMIC;
    }
    else if (identifier_length == 3 && strncmp(identifier, "i32", 3) == 0)
    {
        declaration_type = STACK_TYPE_INTEGER;
    }

    return declaration_type;
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

struct ExecutionContextVariableInfo context_scope_add_variable(struct ExecutionContextScope* scope, const char* name, uint8_t declaration_type)
{
    int index = scope->variable_count++;
    strncpy(scope->variables[index].name, name, MAX_IDENTIFIER_LENGTH);
    scope->variables[index].value = 0;
    scope->variables[index].type = declaration_type;

    #ifdef TOKEN_DEBUG
        printf("Adding variable '%s' (index: %d, type: %s) to scope.\n", name, index, stack_type_names[declaration_type]);
    #endif

    return (struct ExecutionContextVariableInfo) { .index = index, .variable = &scope->variables[index] };
}

void context_set_variable_(struct ExecutionContext* context, struct ExecutionContextVariable* info, uint64_t value, uint8_t type)
{
    if (!(info->type & STACK_TYPE_DYNAMIC))
    {
        if (info->type != type)
        {
            printf("ERR!: Cannot assign to variable, types are incorrect (to: %s, from: %s)\n", stack_type_names[info->type], stack_type_names[type]);
            return;
        }
    }

    info->value = value;
    info->type = type;
}

void context_set_variable(struct ExecutionContext* context, struct ExecutionContextVariableInfo info, uint64_t value, uint8_t type)
{
    context_set_variable_(context, info.variable, value, type);
}

void context_set_variable_ptr(struct ExecutionContext* context, struct ExecutionContextVariableInfo info, void* ptr, uint8_t type)
{
    info.variable->value = (uint64_t)ptr;
    info.variable->type = type;
}

struct ExecutionContextVariableInfo context_add_variable(struct ExecutionContext* context, const char* name, uint8_t declaration_type)
{
    struct ExecutionContextScope* local_scope = context_get_scope(context);

    return context_scope_add_variable(local_scope, name, declaration_type);
}

struct ExecutionContextVariableInfo context_lookup_variable(struct ExecutionContext* context, const char* name)
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
            return (struct ExecutionContextVariableInfo) { .index = lookup, .variable = &context->global_scope.variables[lookup] };
        }

        #ifdef TOKEN_DEBUG
            printf("Variable named '%s' does not exists in scope.\n", name);
        #endif
    
        return (struct ExecutionContextVariableInfo) { .index = -1, .variable = NULL };
    }

    #ifdef TOKEN_DEBUG
        printf("Found variable named '%s' (index: %d).\n", name, lookup);
    #endif

    return (struct ExecutionContextVariableInfo) { .index = lookup, .variable = &local_scope->variables[lookup] };
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

    current = context->code[context->position];

    if (current == '(')
    {
        context->position++;
    }

    context_skip_spaces(context);

    int code_start = context->position;

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

    context_push_stack(context, code_start, STACK_TYPE_FUNCTION);
}

struct ExecutionContextVariableInfo exec_identifier(struct ExecutionContext* context)
{
    char identifier[MAX_IDENTIFIER_LENGTH];
    int identifier_length = parse_identifier(context, identifier, MAX_IDENTIFIER_LENGTH);

    uint8_t declaration_type = context_get_type_from_identifier(context, identifier, identifier_length);
    
    context_skip_spaces(context);

    if (declaration_type != 255)
    {
        parse_identifier(context, identifier, MAX_IDENTIFIER_LENGTH);
    }

    struct ExecutionContextVariableInfo variable = context_lookup_variable(context, identifier);
    
#ifdef TOKEN_DEBUG
    printf("Lookup variable '%s' (index: %d, type: %d)\n", identifier, variable.index, declaration_type);
#endif

    if (variable.index == -1 && declaration_type != 255)
    {
        variable = context_add_variable(context, identifier, declaration_type);
    }

    if (variable.index == -1)
    {
        printf("ERR!: Variable %s is not defined.\n", identifier);
        return variable;
    }

#ifdef TOKEN_DEBUG
    printf("Push variable '%s' (index: %d) to stack\n", identifier, variable.index);
#endif
    
    context_push_stack(context, variable.variable->value, variable.variable->type);
    return variable;
}

void exec_assignment(struct ExecutionContext* context, struct ExecutionContextVariable* variable) 
{
    struct ExecutionContextStackValue value = context_stack_get_value_at_index(context, context->stack_index - 1);

#ifdef TOKEN_DEBUG
    // printf("Assign value '%d' to %p\n", context->stack[context->stack_index - 1], (uint64_t*)context->stack[context->stack_index - 2]);
    printf("Assign value '[%s] %d' to '%s'\n", stack_type_names[value.type], context->stack[context->stack_index - 1], variable->name);
#endif

    context_set_variable_(context, variable, value.value, value.type);
    
    context_pop_stack(context);
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

    // Provided args count pushed to the stack can be calculated
    int args_count = context->stack_index - start_stack_index;

    return args_count;
}

void exec_call_cleanup(struct ExecutionContext* context, int start_stack_index, int args_count) 
{
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

void exec_call_native_function(struct ExecutionContextStackValue stack_value, struct ExecutionContext* context)
{
#ifdef TOKEN_DEBUG
    printf("Calling function at address: %p\n", stack_value.value);
#endif

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

    int args_count = exec_call_args(context, start_stack_index);

    #ifdef TOKEN_DEBUG
        printf("Calling %p with %d arguments\n", func, args_count);
    #endif

    func(context);

    exec_call_cleanup(context, start_stack_index, args_count);
}

void exec_call_function(struct ExecutionContextStackValue stack_value, struct ExecutionContext* context)
{
#ifdef TOKEN_DEBUG
    printf("Calling function at position: %d\n", stack_value.value);
#endif

    if (stack_value.type != STACK_TYPE_FUNCTION)
    {
        printf("ERR!: Value is not a function\n");
        return;
    }

    // Stack value contains start text position for the function, currently
    // it should point to place directly after '(' character
    int func_position = stack_value.value;
    // Save start stack index for later, we need to restore it after
    // function call is done 
    int start_stack_index = context->stack_index;

    #ifdef TOKEN_DEBUG
        printf("Prepare to call %p\n", func_position);
    #endif

    int args_count = exec_call_args(context, start_stack_index);

    int return_position = context->position;

    context->position = func_position;
    context_skip_spaces(context);
    char current = context->code[context->position];

    struct ExecutionContextScope* scope = context_push_scope(context);

    if (current != ')')
    {
        // Args on the stack needs to be assinged to scope variables
        int index = 0;

        do
        {
            struct ExecutionContextStackValue arg_stack_value = context_stack_get_value_at_index(context, start_stack_index + index);
            index++;

            char type_identifier[MAX_IDENTIFIER_LENGTH];
            context_skip_spaces(context);
            int type_identifier_length = parse_identifier(context, type_identifier, MAX_IDENTIFIER_LENGTH);
            context_skip_spaces(context);

            uint8_t type = context_get_type_from_identifier(context, type_identifier, type_identifier_length);

            char identifier[MAX_IDENTIFIER_LENGTH];
            context_skip_spaces(context);
            parse_identifier(context, identifier, MAX_IDENTIFIER_LENGTH);
            context_skip_spaces(context);

            struct ExecutionContextVariableInfo info =  context_scope_add_variable(scope, identifier, type);
            context_set_variable(context, info, arg_stack_value.value, arg_stack_value.type);
            current = context->code[context->position];
        }
        while (current == ',');

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
    
    exec_call_cleanup(context, start_stack_index, args_count);
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

    struct ExecutionContextVariableInfo last_identifier;

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
            last_identifier = exec_identifier(context);

            if (last_identifier.index == -1) 
            {
                continue;
            }

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
            exec_function(context);
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

        context->stack_index = stack_index;

        if (current == '}')
        {
            context->position++;
            break;
        }
    }

    #ifdef TOKEN_DEBUG
        printf("End parsing block\n");
    #endif
}

#pragma region --- SCRIPT FUNCTIONS ---

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

#pragma endregion --- SCRIPT FUNCTIONS ---

void exec(const char* code)
{
    struct ExecutionContext context;
    context.code = code;
    context.code_len = strlen(code);
    context.scope_index = 0;
    context.position = 0;
    context.stack_index = 0;

    context.global_scope.variable_count = 0;
    context_scope_init(&context);

    context_set_variable_ptr(&context, context_scope_add_variable(&context.global_scope, "print", STACK_TYPE_NATIVE_FUNCTION), &fts_print, STACK_TYPE_NATIVE_FUNCTION);
    context_set_variable_ptr(&context, context_scope_add_variable(&context.global_scope, "add", STACK_TYPE_NATIVE_FUNCTION), &fts_add, STACK_TYPE_NATIVE_FUNCTION);

    exec_block(&context);
}