#include "bn.h"
#include <linux/minmax.h>
#include <linux/slab.h>
/*
bool bn_new(bn_t *bn_ptr,unsigned long long length)
{
    bn_ptr->length = 0;
    if ((bn_ptr->num =
            kmalloc(sizeof(unsigned long long) * length, GFP_KERNEL)))
        bn_ptr->length = length;
    return !!bn_ptr->num;
}
*/
/* set all zero */
bool bn_zrenew(bn_t *bn_ptr, unsigned long long length)
{
    if (length == bn_ptr->length) {
        memset(bn_ptr->num, 0, sizeof(unsigned long long) * length);
        return true;
    }
    unsigned long long *tmp =
        krealloc(bn_ptr->num, sizeof(unsigned long long) * length, GFP_KERNEL);
    if (tmp == NULL)
        return false;
    memset(tmp, 0, sizeof(unsigned long long) * length);
    bn_ptr->length = length;
    bn_ptr->num = tmp;
    return true;
}
/* set specific length to zero*/
/*
bool bn_extend(bn_t *bn_ptr, unsigned long long length)
{
    unsigned long long origin = bn_ptr->length;
    if (length < origin)
        return true;
    if (length == origin)
        return true;
    unsigned lomg lonng *tmp =
        krealloc(bn_ptr->num, sizeof(unsigned long long) * length, GFP_KERNEL);
    if (tmp == NULL)
        return false;
    memset(tmp + origin, 0, sizeof(unsigned long long) * (length - origin));
    bn_ptr->length = length;
    bn_ptr->num = tmp;
    return true;
}*/
/* free memory*/
void bn_free(bn_t *bn_ptr)
{
    kfree(bn_ptr->num);
    bn_ptr->length = 0;
    bn_ptr->num = NULL;
}
/* add binary number*/
bool bn_add(const bn_t *a, const bn_t *b, bn_t *res)
{
    if (a->length < b->length ||
        (a->length == b->length &&
         b->num[b->length - 1] > a->num[a->length - 1]))
        swap(a, b);
    if (!bn_zrenew(res, a->length + ((a->num[a->length - 1] &
                                      1LLU << (sizeof(unsigned long long) * 8 -
                                               1)) > 0)))
        return false;
    bn_move(a, res);
    bn_add_carry(b, res, 0);
    return true;
}
/* sub binary number*/
/*
bool bn_sub(const bn_t *a, const bn_t *b, bn_t *res)
{
    if (!bn_zrenew(res, max(a->length, b->length)))
        return false;

    if (a->length > b->length) {
        bn_toggle_move(a, res);// res = -a - 1
        bn_add_carry(b, res, 0);// res  = -a - 1 + b = b - a - 1
        bn_toggle_move(res, res); // res = -res = -(b-a-1)-1 = a - b
    } else {
        bn_toggle_move(b, res); // res = -b - 1
        bn_add_carry(a, res, 1); // res = -b - 1 + a + 1 = a - b
    }
    return true;
}*/
/* move a to res(final number) */
bool bn_move(const bn_t *a, bn_t *res)
{
    if (a->length > res->length)
        return false;
    int i;
    for (i = 0; i < a->length; i++)
        res->num[i] = a->num[i];
    for (; i < res->length; i++)
        res->num[i] = 0;
    return true;
}
/* move 1’s complement of a to res */
/*
bool bn_toggle_move(const bn_t *a, bn_t *res)
{
    if (a->length > res->length)
        return false;
    int i;
    for (i = 0; i < a->length; i++)
        res->num[i] = ~a->num[i];
    for (; i < res->length; i++)
        res->num[i] = ~res->num[i];
    return true;
}
*/
/* add and carry */
void bn_add_carry(const bn_t *b, bn_t *res, int carry)
{
    int i;
    for (i = 0; i < b->length; i++) {
        unsigned long long overflow = b->num[i] & res->num[i];
        unsigned long long msb = b->num[i] | res->num[i];
        res->num[i] += b->num[i] + carry;
        msb &= ~res->num[i];
        overflow |= msb;
        carry = !!(overflow & 1ULL << 63);
    }
    for (; i < res->length; i++) {
        unsigned long long msb = res->num[i];
        res->num[i] += carry;
        msb &= ~res->num[i];
        carry = !!(msb & 1ULL << 63);
    }
}
/* swap a and b*/
void bn_swap(bn_t *a, bn_t *b)
{
    swap(a->length, b->length);
    swap(a->num, b->num);
}