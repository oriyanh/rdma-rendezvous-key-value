#ifndef __TESTS_COMMON_H__
#define __TESTS_COMMON_H__

#include <stdio.h>
#include <stdlib.h>
#define MAX_KEY_LENGTH (1 << 10)
#define MAX_VALUE_LENGTH (1 << 20)

void generate_random_string(char *str, size_t length);
int get_random_key_length();
int get_random_value_length();
int init_random_key_and_value(char **key, char **value);

#endif // __TESTS_COMMON_H__