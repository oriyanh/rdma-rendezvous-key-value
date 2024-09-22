#include "tests_common.h"

void generate_random_string(char *str, size_t length) {
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    size_t charset_size = sizeof(charset) - 1; // Exclude the null terminator

    if (length > 0) {
        for (size_t i = 0; i < length; i++) {
            int random_index = rand() % charset_size;
            str[i] = charset[random_index];
        }
        str[length] = '\0'; // Null-terminate the string
    }
}

int get_random_key_length(){
    return rand() % MAX_KEY_LENGTH + 1;
}

int get_random_value_length(){
    return rand() % MAX_VALUE_LENGTH + 1;
}

int init_random_key_and_value(char **key, char **value){
    int key_length = get_random_key_length();
    int value_length = get_random_value_length();
    *key = (char *)malloc(key_length + 1);
    *value = (char *)malloc(value_length + 1);
    if(*key == NULL || *value == NULL){
        return 1;
    }
    generate_random_string(*key, key_length);
    generate_random_string(*value, value_length);
    return 0;
}