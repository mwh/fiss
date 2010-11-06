// Copyright (C) 2010 Michael Homer <http://mwh.geek.nz>
// Utility functions, mostly for arrays.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <stdlib.h>
#include <string.h>

typedef struct strarray {
    char **array;
    int size;
    int num;
} strarray;

char ** str_array_create(int size) {
    char **ret = malloc(sizeof(char *) * size);
    int i = 0;
    for (i=0; i<size; i++)
        ret[i] = NULL;
    return ret;
}

char *strcop(char *str) {
    char *ret = malloc(strlen(str) + 1);
    strcpy(ret, str);
    return ret;
}

void str_array_set(char **arr, int pos, char *val) {
    if (arr[pos] != NULL)
        free(arr[pos]);
    arr[pos] = strcop(val);
}

char ** str_array_resize(char **arr, int size) {
    char **narr = str_array_create(size * 2);
    int i = 0;
    for (; i<size; i++)
        narr[i] = arr[i];
    return narr;
}

char ** str_array_clear(char **arr, int size) {
    int i;
    for (i=0; i<size; i++) {
        if (arr[i] != NULL) {
            free(arr[i]);
            arr[i] = NULL;
        }
    }
}

strarray *strarray_create(int size) {
    strarray *x = malloc(sizeof(strarray));
    x->size = size;
    x->num = 0;
    x->array = str_array_create(size);
    return x;
}

#define strarray_get(a,b) a->array[b]
void strarray_set(strarray *arr, int pos, char *val) {
    str_array_set(arr->array, pos, val);
}

void strarray_push(strarray *arr, char *val) {
    str_array_set(arr->array, arr->num++, val);
    if (arr->num == arr->size)
        str_array_resize(arr->array, arr->size);
}

void strarray_clear(strarray *arr) {
    str_array_clear(arr->array, arr->size);
    arr->num = 0;
}

void strarray_delete(strarray *arr) {
    strarray_clear(arr);
    free(arr);
}

void strarray_each(strarray *arr, void (*func)(char*)) {
    int i;
    for (i=0; i<arr->num; i++) {
        func(arr->array[i]);
    }
}
void strarray_each1(strarray *arr, void (*func)(char*, void*), void *p1) {
    int i;
    for (i=0; i<arr->num; i++) {
        func(arr->array[i], p1);
    }
}
void strarray_each2(strarray *arr, void (*func)(char*, void*, void*), void *p1,
        void *p2) {
    int i;
    for (i=0; i<arr->num; i++) {
        func(arr->array[i], p1, p2);
    }
}

strarray* strarray_map(strarray *arr, char *(*func)(char *)) {
    int i;
    strarray* ret = strarray_create(arr->size);
    for (i=0; i<arr->num; i++)
        strarray_push(arr, func(strarray_get(arr, i)));
    return ret;
}
strarray* strarray_map1(strarray *arr, char *(*func)(char *, void*), void *p1) {
    int i;
    strarray* ret = strarray_create(arr->size);
    for (i=0; i<arr->num; i++)
        strarray_push(arr, func(strarray_get(arr, i), p1));
    return ret;
}
strarray* strarray_map2(strarray *arr, char *(*func)(char *, void*, void*),
        void *p1, void *p2) {
    int i;
    strarray* ret = strarray_create(arr->size);
    for (i=0; i<arr->num; i++)
        strarray_push(arr, func(strarray_get(arr, i), p1, p2));
    return ret;
}

void* strarray_reduce(strarray *arr, void *(*func)(void*, char*), void *start) {
    int i;
    void *ret = start;
    for (i=0; i<arr->num; i++)
        ret = func(ret, strarray_get(arr, i));
    return ret;
}

char *strarray_join(strarray *arr, char *joiner) {
    int i, len, jlen;
    char *ret;
    jlen = strlen(joiner);
    for (i=0; i<arr->num; i++)
        len += strlen(strarray_get(arr, i)) + jlen;
    ret = malloc(len + 1);
    ret[0] = 0;
    for (i=0; i<arr->num-1; i++) {
        strcat(ret, strarray_get(arr, i));
        strcat(ret, joiner);
    }
    strcat(ret, strarray_get(arr, i));
    return ret;
}
