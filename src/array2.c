#include "array2.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <float.h>
#include <assert.h>

/********************************************************
 Audio Tools, a module and set of tools for manipulating audio data
 Copyright (C) 2007-2011  Brian Langenberger

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *******************************************************/

#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif
#ifndef MAX
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#endif

struct array_i_s* array_i_new(void)
{
    int* data = malloc(sizeof(int) * 1);

    return array_i_wrap(data, 0, 1);
}

struct array_i_s* array_i_wrap(int* data, unsigned size, unsigned total_size)
{
    struct array_i_s* a = malloc(sizeof(struct array_i_s));
    a->data = data;
    a->size = size;
    a->total_size = total_size;

    a->del = array_i_del;
    a->resize = array_i_resize;
    a->reset = array_i_reset;
    a->append = array_i_append;
    a->vappend = array_i_vappend;
    a->extend = array_i_extend;
    a->equals = array_i_equals;
    a->min = array_i_min;
    a->max = array_i_max;
    a->sum = array_i_sum;
    a->copy = array_i_copy;
    a->link = array_i_link;
    a->swap = array_i_swap;
    a->head = array_i_head;
    a->tail = array_i_tail;
    a->de_head = array_i_de_head;
    a->de_tail = array_i_de_tail;
    a->split = array_i_split;
    a->slice = array_i_slice;
    a->reverse = array_i_reverse;
    a->sort = array_i_sort;
    a->print = array_i_print;

    return a;
}

#define ARRAY_DEL(FUNC_NAME, ARRAY_TYPE) \
    void                                      \
    FUNC_NAME(ARRAY_TYPE *array)              \
    {                                         \
        free(array->data);                    \
        free(array);                          \
    }
ARRAY_DEL(array_i_del, array_i)
ARRAY_DEL(array_f_del, array_f)

#define ARRAY_RESIZE(FUNC_NAME, ARRAY_TYPE, ARRAY_DATA_TYPE) \
    void                                                          \
    FUNC_NAME(ARRAY_TYPE *array, unsigned minimum)                \
    {                                                             \
        if (minimum > array->total_size) {                         \
            array->total_size = minimum;                                \
            array->data = realloc(array->data,                          \
                                  sizeof(ARRAY_DATA_TYPE) * minimum);   \
        }                                                               \
    }
ARRAY_RESIZE(array_i_resize, array_i, int)
ARRAY_RESIZE(array_f_resize, array_f, double)

void array_i_reset(array_i *array)
{
    array->size = 0;
}

#define ARRAY_APPEND(FUNC_NAME, ARRAY_TYPE, ARRAY_DATA_TYPE) \
    void                                                          \
    FUNC_NAME(ARRAY_TYPE *array, ARRAY_DATA_TYPE value)           \
    {                                                             \
        if (array->size == array->total_size)                     \
            array->resize(array, array->total_size * 2);          \
                                                                  \
        array->data[array->size++] = value;                       \
    }
ARRAY_APPEND(array_i_append, array_i, int)
ARRAY_APPEND(array_f_append, array_f, double)

#define ARRAY_VAPPEND(FUNC_NAME, ARRAY_TYPE, ARRAY_DATA_TYPE) \
    void                                                           \
    FUNC_NAME(ARRAY_TYPE *array, unsigned count, ...)              \
    {                                                              \
    ARRAY_DATA_TYPE i;                                             \
    va_list ap;                                                    \
                                                                   \
    array->resize(array, array->size + count);                     \
    va_start(ap, count);                                           \
    for (; count > 0; count--) {                                   \
        i = va_arg(ap, ARRAY_DATA_TYPE);                           \
        array->data[array->size++] = i;                            \
    }                                                              \
    va_end(ap);                                                    \
    }
ARRAY_VAPPEND(array_i_vappend, array_i, int)
ARRAY_VAPPEND(array_f_vappend, array_f, double)

#define ARRAY_EXTEND(FUNC_NAME, ARRAY_TYPE, ARRAY_DATA_TYPE) \
    void                                                          \
    FUNC_NAME(ARRAY_TYPE *array, const ARRAY_TYPE *to_add)        \
    {                                                             \
        array->resize(array, array->size + to_add->size);         \
        memcpy(array->data + array->size,                         \
               to_add->data,                                      \
               sizeof(ARRAY_DATA_TYPE) * to_add->size);           \
        array->size += to_add->size;                              \
    }
ARRAY_EXTEND(array_i_extend, array_i, int)
ARRAY_EXTEND(array_f_extend, array_f, double)

#define ARRAY_EQUALS(FUNC_NAME, ARRAY_TYPE, ARRAY_DATA_TYPE) \
    int                                                            \
    FUNC_NAME(const ARRAY_TYPE *array, const ARRAY_TYPE *compare)  \
    {                                                              \
        assert(array->data != NULL);                               \
        assert(compare->data != NULL);                             \
        if (array->size == compare->size) {                        \
            return (memcmp(array->data, compare->data,                  \
                           sizeof(ARRAY_DATA_TYPE) * array->size) == 0); \
        } else                                                          \
            return 0;                                                   \
    }
ARRAY_EQUALS(array_i_equals, array_i, int)
ARRAY_EQUALS(array_f_equals, array_f, double)
ARRAY_EQUALS(array_li_equals, array_li, int)
ARRAY_EQUALS(array_lf_equals, array_lf, double)


#define ARRAY_I_MIN(FUNC_NAME, ARRAY_TYPE) \
    int                                    \
    FUNC_NAME(const ARRAY_TYPE *array)     \
    {                                      \
        int min = INT_MAX;                 \
        unsigned i;                        \
                                           \
        assert(array->data != NULL);       \
        for (i = 0; i < array->size; i++)  \
            if (array->data[i] < min)      \
                min = array->data[i];      \
                                           \
        return min;                        \
    }
ARRAY_I_MIN(array_i_min, array_i)
ARRAY_I_MIN(array_li_min, array_li)

#define ARRAY_I_MAX(FUNC_NAME, ARRAY_TYPE)      \
    int                                          \
    FUNC_NAME(const ARRAY_TYPE *array)           \
    {                                            \
        int max = INT_MIN;                       \
        unsigned i;                              \
                                                 \
        assert(array->data != NULL);             \
        for (i = 0; i < array->size; i++)        \
            if (array->data[i] > max)            \
                max = array->data[i];            \
                                                 \
        return max;                              \
    }
ARRAY_I_MAX(array_i_max, array_i)
ARRAY_I_MAX(array_li_max, array_li)

#define ARRAY_I_SUM(FUNC_NAME, ARRAY_TYPE) \
    int                                         \
    FUNC_NAME(const ARRAY_TYPE *array)          \
    {                                           \
        int accumulator = 0;                    \
        int *data = array->data;                \
        unsigned size = array->size;            \
        unsigned i;                             \
                                                \
        assert(array->data != NULL);            \
        for (i = 0; i < size; i++)              \
            accumulator += data[i];             \
                                                \
        return accumulator;                     \
    }
ARRAY_I_SUM(array_i_sum, array_i)
ARRAY_I_SUM(array_li_sum, array_li)

#define ARRAY_COPY(FUNC_NAME, ARRAY_TYPE, ARRAY_DATA_TYPE) \
    void                                                        \
    FUNC_NAME(const ARRAY_TYPE *array, ARRAY_TYPE *copy)        \
    {                                                           \
        if (array != copy) {                                        \
            array->resize(copy, array->size);                       \
            memcpy(copy->data, array->data,                         \
                   array->size * sizeof(ARRAY_DATA_TYPE));          \
            copy->size = array->size;                               \
        }                                                           \
    }
ARRAY_COPY(array_i_copy, array_i, int)
ARRAY_COPY(array_f_copy, array_f, double)

#define ARRAY_LINK(FUNC_NAME, ARRAY_TYPE, ARRAY_LINK_TYPE) \
    void                                                   \
    FUNC_NAME(const ARRAY_TYPE *array, ARRAY_LINK_TYPE *link)   \
    {                                                           \
        link->data = array->data;                               \
        link->size = array->size;                               \
    }
ARRAY_LINK(array_i_link, array_i, array_li)
ARRAY_LINK(array_f_link, array_f, array_lf)

#define ARRAY_SWAP(FUNC_NAME, ARRAY_TYPE)                  \
    void                                                        \
    FUNC_NAME(ARRAY_TYPE *array, ARRAY_TYPE *swap)              \
    {                                                           \
        ARRAY_TYPE temp;                                        \
        temp.data = array->data;                                \
        temp.size = array->size;                                \
        temp.total_size = array->total_size;                    \
        array->data = swap->data;                               \
        array->size = swap->size;                               \
        array->total_size = swap->total_size;                   \
        swap->data = temp.data;                                 \
        swap->size = temp.size;                                 \
        swap->total_size = temp.total_size;                     \
    }
ARRAY_SWAP(array_i_swap, array_i)
ARRAY_SWAP(array_f_swap, array_f)
ARRAY_SWAP(array_ia_swap, array_ia)
ARRAY_SWAP(array_fa_swap, array_fa)
ARRAY_SWAP(array_iaa_swap, array_iaa)
ARRAY_SWAP(array_faa_swap, array_faa)


#define ARRAY_HEAD(FUNC_NAME, ARRAY_TYPE, ARRAY_DATA_TYPE)     \
    void                                                            \
    FUNC_NAME(const ARRAY_TYPE *array, unsigned count, ARRAY_TYPE *head) \
    {                                                                   \
        unsigned to_copy = MIN(count, array->size);                     \
                                                                        \
        if (head != array) {                                            \
            head->resize(head, to_copy);                                \
            memcpy(head->data, array->data,                             \
                   sizeof(ARRAY_DATA_TYPE) * to_copy);                  \
            head->size = to_copy;                                       \
        } else {                                                        \
            head->size = to_copy;                                       \
        }                                                               \
    }
ARRAY_HEAD(array_i_head, array_i, int)
ARRAY_HEAD(array_f_head, array_f, double)

#define ARRAY_DE_HEAD(FUNC_NAME, ARRAY_TYPE, ARRAY_DATA_TYPE)        \
    void                                                             \
    FUNC_NAME(const ARRAY_TYPE *array, unsigned count, ARRAY_TYPE *tail) \
    {                                                                   \
        unsigned to_copy;                                               \
        count = MIN(count, array->size);                                \
        to_copy = array->size - count;                                  \
                                                                        \
        if (tail != array) {                                            \
            tail->resize(tail, to_copy);                                \
            memcpy(tail->data, array->data + count,                     \
                   sizeof(ARRAY_DATA_TYPE) * to_copy);                  \
            tail->size = to_copy;                                       \
        } else {                                                        \
            memmove(tail->data, array->data + count,                    \
                    sizeof(ARRAY_DATA_TYPE) * to_copy);                 \
            tail->size = to_copy;                                       \
        }                                                               \
    }
ARRAY_DE_HEAD(array_i_de_head, array_i, int)
ARRAY_DE_HEAD(array_f_de_head, array_f, double)

#define ARRAY_TAIL(FUNC_NAME, ARRAY_TYPE, ARRAY_DATA_TYPE)     \
    void                                                            \
    FUNC_NAME(const ARRAY_TYPE *array, unsigned count, ARRAY_TYPE *tail) \
    {                                                                   \
        unsigned to_copy = MIN(count, array->size);                     \
                                                                        \
        if (tail != array) {                                            \
            tail->resize(tail, to_copy);                                \
            memcpy(tail->data, array->data + (array->size - to_copy),   \
                   sizeof(ARRAY_DATA_TYPE) * to_copy);                  \
            tail->size = to_copy;                                       \
        } else {                                                        \
            memmove(tail->data, array->data + (array->size - to_copy),  \
                    sizeof(ARRAY_DATA_TYPE) * to_copy);                 \
            tail->size = to_copy;                                       \
        }                                                               \
    }
ARRAY_TAIL(array_i_tail, array_i, int)
ARRAY_TAIL(array_f_tail, array_f, double)

#define ARRAY_DE_TAIL(FUNC_NAME, ARRAY_TYPE, ARRAY_DATA_TYPE)   \
    void                                                             \
    FUNC_NAME(const ARRAY_TYPE *array, unsigned count, ARRAY_TYPE *head) \
    {                                                                   \
        unsigned to_copy;                                               \
        count = MAX(count, array->size);                                \
        to_copy = array->size - count;                                  \
                                                                        \
        if (head != array) {                                            \
            head->resize(head, to_copy);                                \
            memcpy(head->data, array->data,                             \
                   sizeof(ARRAY_DATA_TYPE) * to_copy);                  \
            head->size = to_copy;                                       \
        } else {                                                        \
            head->size = to_copy;                                       \
        }                                                               \
    }
ARRAY_DE_TAIL(array_i_de_tail, array_i, int)
ARRAY_DE_TAIL(array_f_de_tail, array_f, double)

#define ARRAY_SPLIT(FUNC_NAME, ARRAY_TYPE, ARRAY_DATA_TYPE) \
    void                                                               \
    FUNC_NAME(const ARRAY_TYPE *array, unsigned count,                 \
              ARRAY_TYPE *head, ARRAY_TYPE *tail)                      \
    {                                                                  \
        /*ensure we don't try to move too many items*/                 \
        unsigned to_head = MIN(count, array->size);                    \
        unsigned to_tail = array->size - to_head;                      \
                                                                       \
        if ((head == array) && (tail == array)) {                      \
            /*do nothing*/                                             \
            return;                                                    \
        } else if ((head != array) && (tail == array)) {               \
            /*move "count" values to head and shift the rest down*/    \
            head->resize(head, to_head);                                \
            memcpy(head->data, array->data,                             \
                   sizeof(ARRAY_DATA_TYPE) * to_head);                  \
            head->size = to_head;                                       \
                                                                        \
            memmove(tail->data, array->data + to_head,                  \
                    sizeof(ARRAY_DATA_TYPE) * to_tail);                 \
            tail->size = to_tail;                                       \
        } else if ((head == array) && (tail != array)) {                \
            /*move "count" values from our end to tail and reduce our size*/ \
            tail->resize(tail, to_tail);                                \
            memcpy(tail->data, array->data + to_head,                   \
                   sizeof(ARRAY_DATA_TYPE) * to_tail);                  \
            tail->size = to_tail;                                       \
                                                                        \
            head->size = to_head;                                       \
        } else {                                                        \
            /*copy "count" values to "head" and the remainder to "tail"*/ \
            head->resize(head, to_head);                                \
            memcpy(head->data, array->data,                             \
                   sizeof(ARRAY_DATA_TYPE) * to_head);                  \
            head->size = to_head;                                       \
                                                                        \
            tail->resize(tail, to_tail);                                \
            memcpy(tail->data, array->data + to_head,                   \
                   sizeof(ARRAY_DATA_TYPE) * to_tail);                  \
            tail->size = to_tail;                                       \
        }                                                               \
    }
ARRAY_SPLIT(array_i_split, array_i, int)
ARRAY_SPLIT(array_f_split, array_f, double)

#define ARRAY_SLICE(FUNC_NAME, ARRAY_TYPE, ARRAY_DATA_TYPE, ARRAY_NEW) \
    void                                                            \
    FUNC_NAME(const ARRAY_TYPE *array,                              \
              unsigned start, unsigned end, unsigned jump,          \
              ARRAY_TYPE *slice)                                    \
    {                                                               \
        ARRAY_TYPE* temp;                                           \
        assert(start <= end);                                       \
        assert(jump > 0);                                           \
                                                                    \
        start = MIN(start, array->size);                            \
        end = MIN(end, array->size);                                \
                                                                    \
        if (array != slice) {                                       \
            if (jump == 1) {                                        \
                slice->resize(slice, end - start);                  \
                memcpy(slice->data, array->data + start,            \
                       sizeof(ARRAY_DATA_TYPE) * (end - start));    \
                slice->size = end - start;                          \
            } else {                                                \
                slice->reset(slice);                                \
                for (; start < end; start += jump)                  \
                    slice->append(slice, array->data[start]);       \
            }                                                       \
        } else {                                                    \
            if (jump == 1) {                                        \
                memmove(slice->data, array->data + start,           \
                        sizeof(ARRAY_DATA_TYPE) * (end - start));   \
                slice->size = end - start;                          \
            } else {                                                \
                temp = ARRAY_NEW();                                 \
                for (; start < end; start += jump)                  \
                    temp->append(temp, array->data[start]);         \
                temp->copy(temp, slice);                            \
                temp->del(temp);                                    \
            }                                                       \
        }                                                           \
    }
ARRAY_SLICE(array_i_slice, array_i, int, array_i_new)
ARRAY_SLICE(array_f_slice, array_f, double, array_f_new)

#define ARRAY_REVERSE(FUNC_NAME, ARRAY_TYPE, ARRAY_DATA_TYPE)  \
    void                                                            \
    FUNC_NAME(ARRAY_TYPE *array)                                    \
    {                                                               \
        unsigned i;                                                 \
        unsigned j;                                                 \
        ARRAY_DATA_TYPE x;                                          \
        ARRAY_DATA_TYPE *data = array->data;                        \
                                                                    \
        if (array->size > 0) {                                      \
            for (i = 0, j = array->size - 1; i < j; i++, j--) {     \
                x = data[i];                                        \
                data[i] = data[j];                                  \
                data[j] = x;                                        \
            }                                                       \
        }                                                           \
    }
ARRAY_REVERSE(array_i_reverse, array_i, int)
ARRAY_REVERSE(array_f_reverse, array_f, double)

int array_int_cmp(const void *x, const void *y)
{
    return *(int*)x - *(int*)y;
}

void array_i_sort(array_i *array)
{
    qsort(array->data, (size_t)(array->size), sizeof(int), array_int_cmp);
}

#define ARRAY_I_PRINT(FUNC_NAME, ARRAY_TYPE) \
    void \
    FUNC_NAME(const ARRAY_TYPE *array, FILE* output) \
    {                                                \
        unsigned i;                                  \
                                                     \
        putc('[', output);                           \
        if (array->size == 1) {                      \
            fprintf(output, "%d", array->data[0]);   \
        } else if (array->size > 1) {                \
            for (i = 0; i < array->size - 1; i++)    \
                fprintf(output, "%d, ", array->data[i]);    \
            fprintf(output, "%d", array->data[i]);          \
        }                                                   \
        putc(']', output);                                  \
    }
ARRAY_I_PRINT(array_i_print, array_i)
ARRAY_I_PRINT(array_li_print, array_li)

struct array_li_s* array_li_new(void)
{
    struct array_li_s* array = malloc(sizeof(struct array_li_s));
    array->data = NULL;
    array->size = 0;

    array->del = array_li_del;
    array->equals = array_li_equals;
    array->min = array_li_min;
    array->max = array_li_max;
    array->sum = array_li_sum;
    array->swap = array_li_swap;
    array->head = array_li_head;
    array->tail = array_li_tail;
    array->de_head = array_li_de_head;
    array->de_tail = array_li_de_tail;
    array->split = array_li_split;
    array->print = array_li_print;

    return array;
}

#define ARRAY_L_DEL(FUNC_NAME, ARRAY_TYPE) \
    void                                      \
    FUNC_NAME(ARRAY_TYPE *array)              \
    {                                         \
        free(array);                          \
    }
ARRAY_L_DEL(array_li_del, array_li)
ARRAY_L_DEL(array_lf_del, array_lf)

#define ARRAY_L_SWAP(FUNC_NAME, ARRAY_TYPE)                     \
    void                                                        \
    FUNC_NAME(ARRAY_TYPE *array, ARRAY_TYPE *swap)              \
    {                                                           \
        ARRAY_TYPE temp;                                        \
        temp.data = array->data;                                \
        temp.size = array->size;                                \
        array->data = swap->data;                               \
        array->size = swap->size;                               \
        swap->data = temp.data;                                 \
        swap->size = temp.size;                                 \
    }
ARRAY_L_SWAP(array_li_swap, array_li)
ARRAY_L_SWAP(array_lf_swap, array_lf)

#define ARRAY_L_HEAD(FUNC_NAME, ARRAY_TYPE)                             \
    void                                                                \
    FUNC_NAME(const ARRAY_TYPE *array, unsigned count, ARRAY_TYPE *head) \
    {                                                                   \
        unsigned to_copy = MIN(count, array->size);                     \
        assert(array->data != NULL);                                    \
        head->data = array->data;                                       \
        head->size = to_copy;                                           \
    }
ARRAY_L_HEAD(array_li_head, array_li)
ARRAY_L_HEAD(array_lf_head, array_lf)

#define ARRAY_L_DE_HEAD(FUNC_NAME, ARRAY_TYPE)  \
    void                                                                \
    FUNC_NAME(const ARRAY_TYPE *array, unsigned count, ARRAY_TYPE *tail) \
    {                                                                   \
        unsigned to_copy;                                               \
        assert(array->data != NULL);                                    \
        count = MIN(count, array->size);                                \
        to_copy = array->size - count;                                  \
                                                                        \
        tail->data = array->data + count;                               \
        tail->size = to_copy;                                           \
    }
ARRAY_L_DE_HEAD(array_li_de_head, array_li)
ARRAY_L_DE_HEAD(array_lf_de_head, array_lf)

#define ARRAY_L_TAIL(FUNC_NAME, ARRAY_TYPE) \
    void                                                                \
    FUNC_NAME(const ARRAY_TYPE *array, unsigned count, ARRAY_TYPE *tail) \
    {                                                                   \
        unsigned to_copy = MIN(count, array->size);                     \
        assert(array->data != NULL);                                    \
        tail->data = array->data + (array->size - to_copy);             \
        tail->size = to_copy;                                           \
    }
ARRAY_L_TAIL(array_li_tail, array_li)
ARRAY_L_TAIL(array_lf_tail, array_lf)

#define ARRAY_L_DE_TAIL(FUNC_NAME, ARRAY_TYPE)  \
    void                                                             \
    FUNC_NAME(const ARRAY_TYPE *array, unsigned count, ARRAY_TYPE *head) \
    {                                                                   \
        assert(array->data != NULL);                                    \
        head->size = array->size - MAX(count, array->size);             \
    }
ARRAY_L_DE_TAIL(array_li_de_tail, array_li)
ARRAY_L_DE_TAIL(array_lf_de_tail, array_lf)

#define ARRAY_L_SPLIT(FUNC_NAME, ARRAY_TYPE)            \
    void                                                \
    FUNC_NAME(const ARRAY_TYPE *array, unsigned count,                  \
              ARRAY_TYPE *head, ARRAY_TYPE *tail)                       \
    {                                                                   \
        /*ensure we don't try to move too many items*/                  \
        unsigned to_head = MIN(count, array->size);                     \
        unsigned to_tail = array->size - to_head;                       \
        assert(array->data != NULL);                                    \
                                                                        \
        if ((head == array) && (tail == array)) {                       \
            /*do nothing*/                                              \
            return;                                                     \
        } else {                                                        \
            head->data = array->data;                                   \
            head->size = to_head;                                       \
            tail->data = array->data + to_head;                         \
            tail->size = to_tail;                                       \
        }                                                               \
    }
ARRAY_L_SPLIT(array_li_split, array_li)
ARRAY_L_SPLIT(array_lf_split, array_lf)

array_f* array_f_new(void)
{
    double* data = malloc(sizeof(double) * 1);

    return array_f_wrap(data, 0, 1);
}

void array_f_reset(array_f *array)
{
    array->size = 0;
}

array_f* array_f_wrap(double* data, unsigned size, unsigned total_size)
{
    array_f* a = malloc(sizeof(array_f));
    a->data = data;
    a->size = size;
    a->total_size = total_size;

    a->del = array_f_del;
    a->resize = array_f_resize;
    a->reset = array_f_reset;
    a->append = array_f_append;
    a->vappend = array_f_vappend;
    a->extend = array_f_extend;
    a->equals = array_f_equals;
    a->min = array_f_min;
    a->max = array_f_max;
    a->sum = array_f_sum;
    a->copy = array_f_copy;
    a->link = array_f_link;
    a->swap = array_f_swap;
    a->head = array_f_head;
    a->tail = array_f_tail;
    a->de_head = array_f_de_head;
    a->de_tail = array_f_de_tail;
    a->split = array_f_split;
    a->slice = array_f_slice;
    a->reverse = array_f_reverse;
    a->sort = array_f_sort;
    a->print = array_f_print;

    return a;
}

#define ARRAY_F_MIN(FUNC_NAME, ARRAY_TYPE)      \
    double                                       \
    FUNC_NAME(const ARRAY_TYPE *array)           \
    {                                            \
        double min = DBL_MAX;                    \
        unsigned i;                              \
                                                 \
        assert(array->data != NULL);             \
        for (i = 0; i < array->size; i++)        \
            if (array->data[i] < min)            \
                min = array->data[i];            \
                                                 \
        return min;                              \
    }
ARRAY_F_MIN(array_f_min, array_f)
ARRAY_F_MIN(array_lf_min, array_lf)

#define ARRAY_F_MAX(FUNC_NAME, ARRAY_TYPE)      \
    double                                      \
    FUNC_NAME(const ARRAY_TYPE *array)          \
    {                                           \
        double max = DBL_MIN;                   \
        unsigned i;                             \
                                                \
        assert(array->data != NULL);            \
        for (i = 0; i < array->size; i++)       \
            if (array->data[i] > max)           \
                max = array->data[i];           \
                                                \
        return max;                             \
    }
ARRAY_F_MAX(array_f_max, array_f)
ARRAY_F_MAX(array_lf_max, array_lf)

#define ARRAY_F_SUM(FUNC_NAME, ARRAY_TYPE)      \
    double                                      \
    FUNC_NAME(const ARRAY_TYPE *array)          \
    {                                           \
        double accumulator = 0.0;               \
        double *data = array->data;             \
        unsigned size = array->size;            \
        unsigned i;                             \
                                                \
        assert(array->data != NULL);            \
        for (i = 0; i < size; i++)              \
            accumulator += data[i];             \
                                                \
        return accumulator;                     \
    }
ARRAY_F_SUM(array_f_sum, array_f)
ARRAY_F_SUM(array_lf_sum, array_lf)

int array_float_cmp(const void *x, const void *y)
{
    if (*(double*)x < *(double*)y)
        return -1;
    else if (*(double*)x == *(double*)y)
        return 0;
    else
        return 1;
}

void array_f_sort(array_f *array)
{
    qsort(array->data, (size_t)(array->size), sizeof(double), array_float_cmp);
}

#define ARRAY_F_PRINT(FUNC_NAME, ARRAY_TYPE)            \
    void                                                \
    FUNC_NAME(const ARRAY_TYPE *array, FILE* output)    \
    {                                                   \
        unsigned i;                                     \
                                                        \
        putc('[', output);                              \
        if (array->size == 1) {                          \
            fprintf(output, "%f", array->data[0]);       \
        } else if (array->size > 1) {                    \
            for (i = 0; i < array->size - 1; i++)         \
                fprintf(output, "%f, ", array->data[i]);   \
            fprintf(output, "%f", array->data[i]);         \
        }                                                  \
        putc(']', output);                                 \
    }
ARRAY_F_PRINT(array_f_print, array_f)
ARRAY_F_PRINT(array_lf_print, array_lf)

struct array_lf_s* array_lf_new(void)
{
    struct array_lf_s* array = malloc(sizeof(struct array_lf_s));
    array->data = NULL;
    array->size = 0;

    array->del = array_lf_del;
    array->equals = array_lf_equals;
    array->min = array_lf_min;
    array->max = array_lf_max;
    array->sum = array_lf_sum;
    array->swap = array_lf_swap;
    array->head = array_lf_head;
    array->tail = array_lf_tail;
    array->de_head = array_lf_de_head;
    array->de_tail = array_lf_de_tail;
    array->split = array_lf_split;
    array->print = array_lf_print;

    return array;
}


struct array_ia_s*
array_ia_new(void)
{
    struct array_ia_s* a = malloc(sizeof(struct array_ia_s));
    unsigned i;

    a->data = malloc(sizeof(struct array_i_s*) * 1);
    a->size = 0;
    a->total_size = 1;

    for (i = 0; i < 1; i++) {
        a->data[i] = array_i_new();
    }

    a->del = array_ia_del;
    a->resize = array_ia_resize;
    a->reset = array_ia_reset;
    a->append = array_ia_append;
    a->extend = array_ia_extend;
    a->equals = array_ia_equals;
    a->copy = array_ia_copy;
    a->swap = array_ia_swap;
    a->split = array_ia_split;
    a->print = array_ia_print;

    return a;
}

#define ARRAY_A_DEL(FUNC_NAME, ARRAY_TYPE) \
    void                                   \
    FUNC_NAME(ARRAY_TYPE *array)           \
    {                                      \
        unsigned i;                        \
                                            \
        for (i = 0; i < array->total_size; i++) \
            array->data[i]->del(array->data[i]);    \
                                                    \
        free(array->data);                          \
        free(array);                                \
    }
ARRAY_A_DEL(array_ia_del, array_ia)
ARRAY_A_DEL(array_fa_del, array_fa)
ARRAY_A_DEL(array_iaa_del, array_iaa)
ARRAY_A_DEL(array_faa_del, array_faa)

#define ARRAY_A_RESIZE(FUNC_NAME, ARRAY_TYPE, SUB_ARRAY_TYPE, NEW_FUNC)  \
    void                                                    \
    FUNC_NAME(ARRAY_TYPE *array, unsigned minimum)          \
    {                                                                   \
        if (minimum > array->total_size) {                              \
            array->data = realloc(array->data,                          \
                                  sizeof(SUB_ARRAY_TYPE*) * minimum);   \
            while (array->total_size < minimum) {                       \
                array->data[array->total_size++] = NEW_FUNC();          \
            }                                                           \
        }                                                               \
    }
ARRAY_A_RESIZE(array_ia_resize, array_ia, array_i, array_i_new)
ARRAY_A_RESIZE(array_fa_resize, array_fa, array_f, array_f_new)
ARRAY_A_RESIZE(array_iaa_resize, array_iaa, array_ia, array_ia_new)
ARRAY_A_RESIZE(array_faa_resize, array_faa, array_fa, array_fa_new)

#define ARRAY_A_RESET(FUNC_NAME, ARRAY_TYPE) \
    void                                     \
    FUNC_NAME(ARRAY_TYPE *array)             \
    {                                        \
        unsigned i;                                 \
        for (i = 0; i < array->total_size; i++)           \
            array->data[i]->reset(array->data[i]);  \
        array->size = 0;                            \
    }
ARRAY_A_RESET(array_ia_reset, array_ia)
ARRAY_A_RESET(array_fa_reset, array_fa)
ARRAY_A_RESET(array_iaa_reset, array_iaa)
ARRAY_A_RESET(array_faa_reset, array_faa)

/*note that this does *not* reset the appended sub-array
  prior to returning it

  the array_?a_resize, array_?a_reset and array_?a_split functions
  should always ensure that newly added sub-arrays are reset*/
#define ARRAY_A_APPEND(FUNC_NAME, ARRAY_TYPE, ARRAY_DATA_TYPE) \
    ARRAY_DATA_TYPE*                                           \
    FUNC_NAME(ARRAY_TYPE *array)                               \
    {                                                          \
        if (array->size == array->total_size)                  \
            array->resize(array, array->total_size * 2);       \
                                                               \
        return array->data[array->size++];                     \
    }
ARRAY_A_APPEND(array_ia_append, array_ia, array_i)
ARRAY_A_APPEND(array_fa_append, array_fa, array_f)
ARRAY_A_APPEND(array_iaa_append, array_iaa, array_ia)
ARRAY_A_APPEND(array_faa_append, array_faa, array_fa)

#define ARRAY_A_EXTEND(FUNC_NAME, ARRAY_TYPE)                           \
    void                                                                \
    FUNC_NAME(ARRAY_TYPE *array, const ARRAY_TYPE *to_add)              \
    {                                                                   \
        unsigned i;                                                     \
        for (i = 0; i < to_add->size; i++) {                            \
            to_add->data[i]->copy(to_add->data[i], array->append(array)); \
        }                                                               \
    }
ARRAY_A_EXTEND(array_ia_extend, array_ia)
ARRAY_A_EXTEND(array_fa_extend, array_fa)
ARRAY_A_EXTEND(array_iaa_extend, array_iaa)
ARRAY_A_EXTEND(array_faa_extend, array_faa)

#define ARRAY_A_COPY(FUNC_NAME, ARRAY_TYPE) \
    void                                                        \
    FUNC_NAME(const ARRAY_TYPE *array, ARRAY_TYPE *copy)        \
    {                                                           \
        unsigned i;                                             \
                                                                \
        copy->reset(copy);                                      \
        for (i = 0; i < array->size; i++)                         \
            array->data[i]->copy(array->data[i], copy->append(copy));   \
    }
ARRAY_A_COPY(array_ia_copy, array_ia)
ARRAY_A_COPY(array_fa_copy, array_fa)
ARRAY_A_COPY(array_iaa_copy, array_iaa)
ARRAY_A_COPY(array_faa_copy, array_faa)

#define ARRAY_A_EQUALS(FUNC_NAME, ARRAY_TYPE) \
    int                                               \
    FUNC_NAME(const ARRAY_TYPE *array, const ARRAY_TYPE *compare) \
    {                                                 \
        unsigned i;                                   \
                                                      \
        if (array->size == compare->size) {           \
            for (i = 0; i < array->size; i++)               \
                if (!array->data[i]->equals(array->data[i], \
                                            compare->data[i]))  \
                    return 0;                                   \
                                                                \
            return 1;                                           \
        } else                                                  \
            return 0;                                           \
    }
ARRAY_A_EQUALS(array_ia_equals, array_ia)
ARRAY_A_EQUALS(array_fa_equals, array_fa)
ARRAY_A_EQUALS(array_iaa_equals, array_iaa)
ARRAY_A_EQUALS(array_faa_equals, array_faa)

#define ARRAY_A_PRINT(FUNC_NAME, ARRAY_TYPE) \
    void                                      \
    FUNC_NAME(const ARRAY_TYPE *array, FILE* output)    \
    {                                                   \
        unsigned i;                                     \
                                                        \
        putc('[', output);                              \
        if (array->size == 1) {                             \
            array->data[0]->print(array->data[0], output);  \
        } else if (array->size > 1) {                       \
            for (i = 0; i < array->size - 1; i++) {             \
                array->data[i]->print(array->data[i], output);  \
                printf(", ");                                   \
            }                                                   \
            array->data[i]->print(array->data[i], output);      \
        }                                                       \
        putc(']', output);                                      \
    }
ARRAY_A_PRINT(array_ia_print, array_ia)
ARRAY_A_PRINT(array_fa_print, array_fa)
ARRAY_A_PRINT(array_iaa_print, array_iaa)
ARRAY_A_PRINT(array_faa_print, array_faa)

#define ARRAY_A_SPLIT(FUNC_NAME, ARRAY_TYPE, NEW_FUNC)           \
    void                                                         \
    FUNC_NAME(const ARRAY_TYPE *array, unsigned count,           \
              ARRAY_TYPE *head, ARRAY_TYPE *tail)                \
    {                                                            \
        /*ensure we don't try to move too many items*/           \
        unsigned to_head = MIN(count, array->size);              \
        unsigned to_tail = array->size - to_head;                \
        ARRAY_TYPE *temp;                                        \
        unsigned i;                                              \
                                                                 \
        if ((head == array) && (tail == array)) {                \
            /*do nothing*/                                       \
            return;                                              \
        } else if ((head != array) && (tail == array)) {            \
            /*move "count" values to head and shift the rest down*/ \
                                                                    \
            head->reset(head);                                      \
            for (i = 0; i < to_head; i++)                             \
                array->data[i]->copy(array->data[i], head->append(head)); \
                                                                        \
            temp = NEW_FUNC();                                          \
            for (; i < array->size; i++)                                \
                array->data[i]->swap(array->data[i], temp->append(temp)); \
                                                                        \
            tail->reset(tail);                                          \
            for (i = 0; i < to_tail; i++)                               \
                temp->data[i]->swap(temp->data[i], tail->append(tail)); \
                                                                        \
            temp->del(temp);                                            \
        } else if ((head == array) && (tail != array)) {                \
            /*move "count" values from our end to tail and reduce our size*/ \
                                                                        \
            tail->reset(tail);                                          \
            for (i = to_head; i < array->size; i++) {                   \
                array->data[i]->swap(array->data[i], tail->append(tail)); \
                array->data[i]->reset(array->data[i]);                  \
            }                                                           \
            head->size = to_head;                                       \
        } else {                                                        \
            /*copy "count" values to "head" and the remainder to "tail"*/ \
                                                                        \
            head->reset(head);                                          \
            for (i = 0; i < to_head; i++)                               \
                array->data[i]->copy(array->data[i], head->append(head)); \
                                                                        \
            tail->reset(tail);                                          \
            for (; i < array->size; i++)                                \
                array->data[i]->copy(array->data[i], tail->append(tail)); \
        }                                                               \
    }
ARRAY_A_SPLIT(array_ia_split, array_ia, array_ia_new)
ARRAY_A_SPLIT(array_fa_split, array_fa, array_fa_new)
ARRAY_A_SPLIT(array_iaa_split, array_iaa, array_iaa_new)
ARRAY_A_SPLIT(array_faa_split, array_faa, array_faa_new)


array_fa*
array_fa_new(void)
{
    array_fa* a = malloc(sizeof(array_f));
    unsigned i;

    a->data = malloc(sizeof(array_f*) * 1);
    a->size = 0;
    a->total_size = 1;

    for (i = 0; i < 1; i++) {
        a->data[i] = array_f_new();
    }

    a->del = array_fa_del;
    a->resize = array_fa_resize;
    a->reset = array_fa_reset;
    a->append = array_fa_append;
    a->extend = array_fa_extend;
    a->equals = array_fa_equals;
    a->copy = array_fa_copy;
    a->swap = array_fa_swap;
    a->split = array_fa_split;
    a->print = array_fa_print;

    return a;
}

struct array_iaa_s* array_iaa_new(void)
{
    struct array_iaa_s* a = malloc(sizeof(struct array_iaa_s));
    unsigned i;

    a->data = malloc(sizeof(struct array_ia_s*) * 1);
    a->size = 0;
    a->total_size = 1;

    for (i = 0; i < 1; i++) {
        a->data[i] = array_ia_new();
    }

    a->del = array_iaa_del;
    a->resize = array_iaa_resize;
    a->reset = array_iaa_reset;
    a->append = array_iaa_append;
    a->extend = array_iaa_extend;
    a->equals = array_iaa_equals;
    a->copy = array_iaa_copy;
    a->swap = array_iaa_swap;
    a->split = array_iaa_split;
    a->print = array_iaa_print;

    return a;
}

struct array_faa_s* array_faa_new(void)
{
    struct array_faa_s* a = malloc(sizeof(struct array_faa_s));
    unsigned i;

    a->data = malloc(sizeof(struct array_fa_s*) * 1);
    a->size = 0;
    a->total_size = 1;

    for (i = 0; i < 1; i++) {
        a->data[i] = array_fa_new();
    }

    a->del = array_faa_del;
    a->resize = array_faa_resize;
    a->reset = array_faa_reset;
    a->append = array_faa_append;
    a->extend = array_faa_extend;
    a->equals = array_faa_equals;
    a->copy = array_faa_copy;
    a->swap = array_faa_swap;
    a->split = array_faa_split;
    a->print = array_faa_print;

    return a;
}
