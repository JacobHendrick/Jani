#include "jani_libc.h"

int jani_memcmp(const void *left, const void *right, size_t count) {
    const unsigned char *a = left;
    const unsigned char *b = right;
    size_t index;

    for (index = 0; index < count; index++) {
        if (a[index] != b[index]) {
            return (a[index] < b[index]) ? -1 : 1;
        }
    }

    return 0;
}

int jani_strcmp(const char *left, const char *right) {
    size_t index;

    for (index = 0; left[index] != '\0'; index++) {
        if (left[index] != right[index]) {
            break;
        }
    }

    if ((unsigned char)left[index] == (unsigned char)right[index]) {
        return 0;
    }

    return ((unsigned char)left[index] < (unsigned char)right[index]) ? -1 : 1;
}

int jani_strncmp(const char *left, const char *right, size_t count) {
    size_t index;

    for (index = 0; index < count; index++) {
        if (left[index] != right[index]) {
            return ((unsigned char)left[index] < (unsigned char)right[index])
                       ? -1
                       : 1;
        }
        if (left[index] == '\0') {
            return 0;
        }
    }

    return 0;
}

char *jani_strcpy(char *destination, const char *source) {
    size_t index;

    for (index = 0; source[index] != '\0'; index++) {
        destination[index] = source[index];
    }
    destination[index] = '\0';

    return destination;
}

char *jani_strncpy(char *destination, const char *source, size_t count) {
    size_t index;

    for (index = 0; (index < count) && (source[index] != '\0'); index++) {
        destination[index] = source[index];
    }
    for (; index < count; index++) {
        destination[index] = '\0';
    }

    return destination;
}

char *jani_strchr(const char *text, int character) {
    char target = (char)character;
    size_t index;

    for (index = 0; text[index] != '\0'; index++) {
        if (text[index] == target) {
            return (char *)&text[index];
        }
    }

    if (target == '\0') {
        return (char *)&text[index];
    }

    return 0;
}

char *jani_strrchr(const char *text, int character) {
    char target = (char)character;
    const char *found = 0;
    size_t index;

    for (index = 0; text[index] != '\0'; index++) {
        if (text[index] == target) {
            found = &text[index];
        }
    }

    if (target == '\0') {
        return (char *)&text[index];
    }

    return (char *)found;
}

char *jani_strstr(const char *haystack, const char *needle) {
    size_t outer;
    size_t inner;

    if (needle[0] == '\0') {
        return (char *)haystack;
    }

    for (outer = 0; haystack[outer] != '\0'; outer++) {
        for (inner = 0; needle[inner] != '\0'; inner++) {
            if (haystack[outer + inner] != needle[inner]) {
                break;
            }
        }
        if (needle[inner] == '\0') {
            return (char *)&haystack[outer];
        }
    }

    return 0;
}
