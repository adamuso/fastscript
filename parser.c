#include "context.h"

#include <ctype.h>

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