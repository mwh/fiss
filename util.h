
typedef struct strarray {
    char **array;
    int size;
    int num;
} strarray;

char *strcop(char *str);

//char ** str_array_create(int size);
//void str_array_set(char **arr, int pos, char *val);
//char ** str_array_resize(char **arr, int size);
//char ** str_array_clear(char **arr, int size);
strarray *strarray_create(int size);
void strarray_set(strarray *arr, int pos, char *val);
//char *strarray_get(strarray *arr, int pos);
void strarray_push(strarray *arr, char *val);
void strarray_clear(strarray *arr);

// Not really useful.
/*
void strarray_each(strarray *arr, void (*func)(char*));
void strarray_each1(strarray *arr, void (*func)(char*, void*), void *p1);
void strarray_each2(strarray *arr, void (*func)(char*, void*, void*), void *p1,
        void *p2);
strarray* strarray_map(strarray *arr, char *(*func)(char *));
strarray* strarray_map1(strarray *arr, char *(*func)(char *, void*), void *p1);
strarray* strarray_map2(strarray *arr, char *(*func)(char *, void*, void*),
        void *p1, void *p2);/**/
#define strarray_get(a,b) a->array[b]
