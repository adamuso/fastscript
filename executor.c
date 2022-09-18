#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "executor.h"
#include "context.h"
#include "parser.h"
#include "debug.h"

void exec_expression(struct ExecutionContext* context);

#pragma region --- Block ---

void exec_block(struct ExecutionContext* context)
{
    char current;

    #ifdef TOKEN_DEBUG
        debug("Parsing block\n");
    #endif

    int block_stack_index = context->stack_index;
    int block_stack_variables = context->stack_variables;

    while (context->position < context->code_len)
    {
        int stack_index = context->stack_index;

#ifdef TOKEN_DEBUG
        debug("\n\n--- Parsing statement (stack_index: %d) ---\n", stack_index);
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
            debug("--- End parsing statement (stack_index: %d) ---\n", context->stack_index);
        #endif

        struct ExecutionContextStackIterator iterator = context_stack_iterate(context);

        while(context->stack_index != context->stack_variables)
        {
            context_stack_pop_value(context);
        } 

        if (current == '}')
        {
            context->position++;
            break;
        }
    }

    context->stack_index = block_stack_index;
    context->stack_variables = block_stack_variables;

    #ifdef TOKEN_DEBUG
        debug("End parsing block\n");
    #endif
}

#pragma endregion --- Block ---

#pragma region --- Literals ---
#pragma region Simple literals

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
        debug("Push number '%s' (flags: %d, value: %f) to stack\n", number, flags, value);
        break;
    case 0x0:
    case 0x4:
    case 0x8:
    case 0x12:
        debug("Push number '%s' (flags: %d, value: %d) to stack\n", number, flags, value);
    default:
        break;
    }
#endif

    context_stack_push_value(
        context, 
        (struct ExecutionContextStackValue) { .ptr = &value, .type = NATIVE_TYPE_I32, .size = get_size_of_native_type(NATIVE_TYPE_I32) }
    );
}

#pragma endregion Simple literals

#pragma region Function literal

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
    debug("Function declaration at: %d\n", code_start);
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
        (struct ExecutionContextStackValue) { .ptr = &code_start, .type = NATIVE_TYPE_FUNCTION, .size = get_size_of_native_type(NATIVE_TYPE_FUNCTION) }
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

#pragma endregion Function literal

#pragma region Struct literal

void exec_struct_field(struct ExecutionContext* context, struct ExecutionContextStructDefinition* definition)
{
    char type_identifier[MAX_IDENTIFIER_LENGTH];
    context_skip_spaces(context);
    int type_identifier_length = parse_identifier(context, type_identifier, MAX_IDENTIFIER_LENGTH);
    context_skip_spaces(context);

    char current = context->code[context->position];

    struct ExecutionContextTypeInfo type_info = 
        context_get_type_from_identifier(context, type_identifier, type_identifier_length, NULL);

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

        field_definition.type = (struct ExecutionContextTypeInfo) { .native = NATIVE_TYPE_FUNCTION, .complex = NULL };
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
        debug("ERR!: Epected '{'\n");
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
            debug("ERR!: Syntax error missing ')'\n");
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

#pragma endregion Struct literal
#pragma endregion --- Literals ---

#pragma region --- Access ---
#pragma region Variables

void exec_access_variable(struct ExecutionContext* context, struct ExecutionContextVariable* variable) 
{
    context_variable_push_into_stack(context, variable);
}

void exec_assignment(struct ExecutionContext* context, struct ExecutionContextVariable* variable) 
{
    struct ExecutionContextStackValue value = context_stack_get_last_value(context);

#ifdef TOKEN_DEBUG
    debug("Assign value '[%s] %d' to '%s'\n", get_stack_type_name(value.type), *value.ptr, variable->name);
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
        debug("ERR!: Expected '=' for '%s' variable declaration.\n", identifier);
    }
    else 
    {
        context->position++;
    }

    exec_expression(context);

    struct ExecutionContextStackValue value = context_stack_get_last_value(context);

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
        debug("ERR!: Cannot add local variable '%s'.\n", identifier);
        return;
    }

    context_variable_set_value(context, variable, value);

#ifdef TOKEN_DEBUG
    debug("Declared variable '%s' with value '[%s] %d'\n", variable->name, get_stack_type_name(value.type), context->stack[context->stack_index - 1]);
#endif

}

#pragma endregion Variables

#pragma region Struct fields

void exec_field_access(struct ExecutionContext* context)
{
    struct ExecutionContextStackValue value = context_stack_get_last_value(context);
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

#pragma endregion Struct fields
#pragma endregion --- Access ---

#pragma region --- Operators ---

#pragma region Call

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
        debug("ERR!: Syntax error missing ')'\n");
        // FIXME: should panic
        return 0;
    }

    // Provided args size pushed to the stack can be calculated
    int args_stack_size = context->stack_index - start_stack_index;

    return args_stack_size;
}

void exec_call_cleanup(struct ExecutionContext* context, int frame_start_stack_index, int args_stack_size) 
{
    struct ExecutionContextStackIterator iterator = context_stack_iterate(context);

    int return_count = 0;
    int frame_args_end_stack_index = frame_start_stack_index + args_stack_size;

    while(iterator.stack_index != frame_args_end_stack_index)
    {
        // bypass returned values
        context_stack_iterator_next(context, &iterator);
        return_count++;
    }

    int return_size = context->stack_index - iterator.stack_index;

    #ifdef TOKEN_DEBUG
    debug("Returned values %d, overall size: %d byte(s)\n", return_count, return_size);
    #endif

    while(iterator.stack_index != frame_start_stack_index)
    {
        // destruct all args values
        context_stack_iterator_next(context, &iterator);
        context_stack_unset_value_at_index(context, iterator.stack_index);
    }

    memmove(&context->stack[frame_start_stack_index], &context->stack[frame_args_end_stack_index], return_size);
    context->stack_index = frame_start_stack_index + return_size;

    #ifdef TOKEN_DEBUG
    if (return_count)
    {
        struct ExecutionContextStackValue value = context_stack_get_last_value(context);
        debug("Last returned value: %d (type: %s, size: %d)\n", *value.ptr, get_stack_type_name(value.type), value.size);
    }
    #endif
}

void exec_call_native_function(struct ExecutionContextStackValue stack_value, struct ExecutionContext* context)
{
#ifdef TOKEN_DEBUG
    debug("Calling function at address: %p\n", *stack_value.ptr);
#endif

    if (stack_value.type != NATIVE_TYPE_NATIVE_FUNCTION)
    {
        debug("ERR!: Value is not a function\n");
        return;
    }

    char current = context->code[context->position];

    void(*func)(struct ExecutionContext*) = *(void**)stack_value.ptr;
    int frame_start_stack_index = context->stack_index;

    #ifdef TOKEN_DEBUG
        debug("Prepare to call %p\n", func);
    #endif

    int args_count = exec_call_args(context, frame_start_stack_index);

    #ifdef TOKEN_DEBUG
        debug("Calling %p with %d arguments\n", func, args_count);
    #endif

    func(context);

    exec_call_cleanup(context, frame_start_stack_index, args_count);
}

void exec_call_function(struct ExecutionContextStackValue stack_value, struct ExecutionContext* context)
{
#ifdef TOKEN_DEBUG
    debug("Calling function at position: %d\n", *stack_value.ptr);
#endif

    if (stack_value.type != NATIVE_TYPE_FUNCTION)
    {
        debug("ERR!: Value is not a function\n");
        return;
    }

    // Stack value contains start text position for the function, currently
    // it should point to place directly after '(' character
    int func_position = *stack_value.ptr;
    // Save start stack index for later, we need to restore it after
    // function call is done 
    int frame_start_stack_index = context->stack_index;

    #ifdef TOKEN_DEBUG
        debug("Prepare to call %p\n", func_position);
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

            struct ExecutionContextTypeInfo type_info = context_get_type_from_identifier(context, type_identifier, type_identifier_length, NULL);

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
            debug("ERR!: Syntax error missing ')'\n");
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
    struct ExecutionContextStackValue stack_value = context_stack_get_last_value(context);

    if (stack_value.type == NATIVE_TYPE_NATIVE_FUNCTION)
    {
        exec_call_native_function(stack_value, context);
    }
    else if (stack_value.type == NATIVE_TYPE_FUNCTION)
    {
        exec_call_function(stack_value, context);
    }
    else 
    {
        debug("ERR!: Value is not a function\n");
    }
}

#pragma endregion Call

#pragma endregion --- Operators ---

struct ExectionContextIdentifierResult exec_identifier(struct ExecutionContext* context, char* identifier, int max_len)
{
    int identifier_length = parse_identifier(context, identifier, max_len);

    // Check if identifier is a keyword

    if (identifier_length == 6 && strncmp(identifier, "struct", identifier_length) == 0)
    {
        exec_struct(context);
        return (struct ExectionContextIdentifierResult) { .data_type = EXECUTION_CONTEXT_IDENTIFIER_RESULT_HANDLED };
    }

    // If not a keyword the try to get type from it

    // context_get_type_from_identifier searcher for variable and we can remember the search
    // result to not search again
    struct ExecutionContextVariable* variable = NULL;

    struct ExecutionContextTypeInfo type_info = 
        context_get_type_from_identifier(context, identifier, identifier_length, &variable);

    struct ExectionContextIdentifierResult result;

    if (type_info.native != 255)
    {
        result.data_type = EXECUTION_CONTEXT_IDENTIFIER_RESULT_TYPE;
        result.type_data = type_info;
        return result;
    }

    // When identifier was not a type then we should look for a variable
    // and push it to the stack 

    if (!variable) 
    {
        variable = context_lookup_variable(context, identifier);
    }

    if (!variable)
    {
        debug("ERR!: Variable %s is not defined in current scope.\n", identifier);
        result.data_type = EXECUTION_CONTEXT_IDENTIFIER_RESULT_ERROR;
        return result;
    }

    exec_access_variable(context, variable);

    result.data_type = EXECUTION_CONTEXT_IDENTIFIER_RESULT_VARIABLE;
    result.variable_data = variable;

    return result;
}

void exec_expression(struct ExecutionContext* context)
{
    char current;
    char last_expression = 0;

    struct ExectionContextIdentifierResult last_identifier_result;

    last_identifier_result.data_type = EXECUTION_CONTEXT_IDENTIFIER_RESULT_HANDLED;

    #ifdef TOKEN_DEBUG
        debug("Parsing expression\n");
    #endif

    while (context->position < context->code_len)
    {
        context_skip_spaces(context);
        current = context->code[context->position];

        #ifdef TOKEN_DEBUG
            debug("TOKEN: %c\n", current);
        #endif

        if (isalpha(current)) 
        {
            char identifier[MAX_IDENTIFIER_LENGTH];

            if (last_identifier_result.data_type == EXECUTION_CONTEXT_IDENTIFIER_RESULT_TYPE)
            {
                last_identifier_result.data_type = EXECUTION_CONTEXT_IDENTIFIER_RESULT_HANDLED;

                // Last expression was identifier representing a type, so this is a variable declaration
                context_skip_spaces(context);

                int identifier_length = parse_identifier(context, identifier, MAX_IDENTIFIER_LENGTH);

                struct ExecutionContextVariable* variable = context_lookup_variable(context, identifier);
                
                if (variable)
                {
                    debug("ERR!: Variable %s is aready defined.\n", identifier);
                    return;
                }

                exec_variable_declaration(context, identifier, last_identifier_result.type_data);
                continue;
            }        

            last_identifier_result = exec_identifier(context, identifier, MAX_IDENTIFIER_LENGTH);

            if (last_identifier_result.data_type >= EXECUTION_CONTEXT_IDENTIFIER_RESULT_ERROR)
            {
                debug("ERR!: Identifier %s is not known identifier\n", identifier);
                return;
            }

            continue;
        }
        else if (isdigit(current) || current == '.')
        {
            if (current == '.')
            {
                struct ExecutionContextStackValue value = context_stack_get_last_value(context);

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
            struct ExecutionContextStackValue value = context_stack_get_last_value(context);

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
            debug("unknown char %c\n", current);            
            context->position++;
        }
        
        last_identifier_result.data_type = EXECUTION_CONTEXT_IDENTIFIER_RESULT_HANDLED;
    }

    #ifdef TOKEN_DEBUG
        debug("End parsing expression\n");
    #endif
}

#pragma region --- SCRIPT FUNCTIONS ---

void fts_print(struct ExecutionContext* context)
{
    struct ExecutionContextStackIterator iterator = context_stack_iterate(context);
    struct ExecutionContextStackValue value = context_stack_iterator_next(context, &iterator);

    if (value.type == NATIVE_TYPE_I32) 
    {
        debug("%d\n", context->stack[context->stack_index - 1]);
    }
    else 
    {
        debug("Invalid type\n");
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
        context_add_global_variable(&context, "print", NATIVE_TYPE_NATIVE_FUNCTION, 1), 
        (struct ExecutionContextStackValue) { .ptr = &fts_print_value, .type = NATIVE_TYPE_NATIVE_FUNCTION, .size = get_size_of_native_type(NATIVE_TYPE_NATIVE_FUNCTION) }
    );
    
    uint64_t fts_add_value = (uint64_t)&fts_add;

    context_variable_set_value(
        &context, 
        context_add_global_variable(&context, "add", NATIVE_TYPE_NATIVE_FUNCTION, 1), 
        (struct ExecutionContextStackValue) { .ptr = &fts_add_value, .type = NATIVE_TYPE_NATIVE_FUNCTION, .size = get_size_of_native_type(NATIVE_TYPE_NATIVE_FUNCTION) }
    );

    exec_block(&context);
}