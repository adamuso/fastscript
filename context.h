#include <stdint.h>
#include <stdbool.h>

#include "defs.h"
#include "object.h"

#pragma region --- CONTEXT ---

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
);

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

struct ExecutionContextVariable
{
    char name[MAX_IDENTIFIER_LENGTH];
    int stack_index;
};

struct ExecutionContextScope
{
    struct ExecutionContextVariable variables[16];
    int variable_count;
    int min_stack_index;
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
    struct ExecutionContextScope* global_scope;
    struct ExecutionContextScope scopes[16];
    int scope_index;
    uint64_t stack[64];
    uint8_t stack_type[64];
    int stack_index;
    int stack_variables;
};

enum ExecutionContextIdentifierResultType
{
    EXECUTION_CONTEXT_IDENTIFIER_RESULT_HANDLED,
    EXECUTION_CONTEXT_IDENTIFIER_RESULT_TYPE,
    EXECUTION_CONTEXT_IDENTIFIER_RESULT_VARIABLE,
    EXECUTION_CONTEXT_IDENTIFIER_RESULT_VALUE,
    EXECUTION_CONTEXT_IDENTIFIER_RESULT_ERROR,
};

struct ExecutionContextIdentifierResult 
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

int context_eof(struct ExecutionContext* context);
void context_skip_spaces(struct ExecutionContext* context);
bool check_type_is_assignable_to(uint8_t current_type, uint8_t new_type);
int get_size_of_native_type(uint8_t type);
int get_size_of_type(struct ExecutionContextTypeInfo type_info);
void destruct_struct(struct ExecutionContextStructDefinition* definition, uint8_t* data);
void destruct_field_list(struct ExecutionContextStructDefinitionFieldList* fields, uint8_t* data);
void destruct_struct(struct ExecutionContextStructDefinition* definition, uint8_t* data);

#pragma region --- CONTEXT STACK ---

struct ExecutionContextStackValue context_stack_get_value_at_index(struct ExecutionContext* context, int index);
struct ExecutionContextStackValue context_stack_get_last_value(struct ExecutionContext* context);
void context_stack_reset_value_at_index(struct ExecutionContext* context, int index, struct ExecutionContextStackValue value);
struct ExecutionContextStackValue context_stack_unset_value_at_index(struct ExecutionContext* context, int index);
void context_stack_set_value_at_index(struct ExecutionContext* context, int index, struct ExecutionContextStackValue value);

int context_stack_push_value(struct ExecutionContext* context, struct ExecutionContextStackValue value);
int context_stack_pop_value(struct ExecutionContext* context);

struct ExecutionContextStackIterator context_stack_iterate(struct ExecutionContext* context);
struct ExecutionContextStackValue context_stack_iterator_next(struct ExecutionContext* context, struct ExecutionContextStackIterator* iterator);

#pragma endregion --- CONTEXT STACK ---

#pragma region --- CONTEXT SCOPE ---

void context_scope_init(struct ExecutionContext* context);
struct ExecutionContextScope* context_get_scope(struct ExecutionContext* context);
struct ExecutionContextScope* context_push_scope(struct ExecutionContext* context);
struct ExecutionContextScope* context_pop_scope(struct ExecutionContext* context);

int context_scope_variables_binary_search(struct ExecutionContextScope* scope, int l, int r, const char* x);
int context_scope_variables_add_sorted(struct ExecutionContextScope* scope, int n, const char* x, int capacity);
int context_scope_variables_linear_search(struct ExecutionContextScope* scope, const char* x);

struct ExecutionContextVariable* context_scope_add_variable(
    struct ExecutionContextScope* scope, 
    const char* name, 
    int stack_index
);

#pragma endregion --- CONTEXT SCOPE ---

void context_variable_set_value(struct ExecutionContext* context, struct ExecutionContextVariable* info, struct ExecutionContextStackValue value);
struct ExecutionContextStackValue context_variable_get_value(struct ExecutionContext* context, struct ExecutionContextVariable* variable);
void context_variable_push_into_stack(struct ExecutionContext* context, struct ExecutionContextVariable* variable);

struct ExecutionContextVariable* context_add_variable(
    struct ExecutionContext* context, 
    struct ExecutionContextScope* scope, 
    const char* name, 
    uint8_t declaration_type, 
    size_t size_in_bytes, 
    bool override
);

struct ExecutionContextVariable* context_add_local_variable(
    struct ExecutionContext* context, 
    const char* name, 
    uint8_t declaration_type, 
    size_t size_in_bytes
);

struct ExecutionContextVariable* context_add_global_variable(
    struct ExecutionContext* context, 
    const char* name, 
    uint8_t declaration_type, 
    size_t size_in_bytes
);

struct ExecutionContextVariable* context_lookup_variable(struct ExecutionContext* context, const char* name);

struct ExecutionContextTypeInfo context_get_type_from_identifier(
    struct ExecutionContext* context,
    const char* identifier, 
    int identifier_length,
    struct ExecutionContextVariable** variable
);

#pragma endregion --- CONTEXT ---
