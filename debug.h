#pragma once

#define TEST_DEBUG
#define TOKEN_DEBUG

#ifdef TOKEN_DEBUG
#include <stdio.h>

#define debug(format, ...) printf("[at %s (%s:%d)] " format, __PRETTY_FUNCTION__, __FILE__, __LINE__, ##__VA_ARGS__)

const char* get_stack_type_name(int type);
#endif