#include "bn.h"
#include <linux/minmax.h>
#include <linux/slab.h>
/* new binaray */
bool bn_new(bn_t *bn_ptr, unsigned long long length)
{
    bn_ptr->length = 0;
    if ((bn_ptr->num =
             kmalloc(sizeof(unsigned long long) * length, GFP_KERNEL)))
        bn_ptr->length = length;
    return !!bn_ptr->num;
}
/* new zeor binaray */
bool bn_znew(bn_t *bn_ptr, unsigned long long length)
{
    bn_ptr->length = 0;
    if ((bn_ptr->num =
             kzalloc(sizeof(unsigned long long) * length, GFP_KERNEL)))
        bn_ptr->length = length;
    return !!bn_ptr->num;
}
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
bool bn_extend(bn_t *bn_ptr, unsigned long long length)
{
    unsigned long long origin = bn_ptr->length;
    if (length < origin)
        return true;
    if (length == origin)
        return true;
    unsigned long long *tmp =
        krealloc(bn_ptr->num, sizeof(unsigned long long) * length, GFP_KERNEL);
    if (tmp == NULL)
        return false;
    memset(tmp + origin, 0, sizeof(unsigned long long) * (length - origin));
    bn_ptr->length = length;
    bn_ptr->num = tmp;
    return true;
}
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
bool bn_sub(const bn_t *a, const bn_t *b, bn_t *res)
{
    if (!bn_zrenew(res, max(a->length, b->length)))
        return false;

    if (a->length > b->length) {
        bn_toggle_move(a, res);    // res = -a - 1
        bn_add_carry(b, res, 0);   // res  = -a - 1 + b = b - a - 1
        bn_toggle_move(res, res);  // res = -res = -(b-a-1)-1 = a - b
    } else {
        bn_toggle_move(b, res);   // res = -b - 1
        bn_add_carry(a, res, 1);  // res = -b - 1 + a + 1 = a - b
    }
    return true;
}
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
/* left shift */
bool bn_lshift(bn_t *res, unsigned long long bits)
{
    if (!bits)
        return false;
    unsigned long long shift_len = (bits >> 3) / sizeof(shift_len) + 1;
    unsigned long long mod_bits = bits & 0x3fULL;
    bool remained = true;

    if (shift_len > res->length)
        remained = false;
    else
        for (int i = res->length - shift_len; i < res->length; i++)
            if (res->num[i] > 0)
                remained = false;

    if (!remained && !bn_extend(res, shift_len + res->length))
        return false;

    for (int i = res->length - 1; i >= shift_len; i--)
        res->num[i] = (res->num[i - shift_len + 1] << mod_bits) |
                      ((res->num[i - shift_len] >> (63 - shift_len)) >> 1);
    res->num[shift_len - 1] = res->num[0] << mod_bits;
    for (int i = 0; i < shift_len - 1; i++)
        res->num[i] = 0;
    return true;
}
/* right shift */
void bn_rshift(bn_t *res, unsigned long long bits)
{
    if (!bits)
        return;
    unsigned long long shift_len = bits / 64;
    unsigned long long mod_bits = bits & 0x3fULL;

    if (shift_len > res->length) {
        memset(res->num, 0, res->length * sizeof(unsigned long long));
        res->length = 1;
        return;
    }
    int i;
    for (i = 0; i < res->length - shift_len - 1; i++)
        res->num[i] = (res->num[i + shift_len] << mod_bits) |
                      ((res->num[i + shift_len + 1] << (63 - shift_len)) << 1);
    res->num[res->length - shift_len - 1] =
        res->num[res->length - 1] >> mod_bits;
    for (i = res->length - shift_len; i < res->length - 1; i++)
        res->num[i] = 0;
    res->length = res->length - shift_len;
    return;
}
/* mask */
void bn_mask(bn_t *ptr, unsigned long long msk)
{
    for (int i = 0; i < ptr->length; i++)
        ptr->num[i] &= msk;
}
/* shrink */
bool bn_shrink(bn_t *ptr)
{
    int i;
    for (i = ptr->length - 1; i >= 0; i--) {
        if (ptr->num[i] > 0)
            break;
    }
    ptr->length = i + 1;
    if (ptr->length == 0)
        ptr->length = 1;
    return true;
}
/* multiply */
bool bn_mult(bn_t *a, bn_t *res)
{
    bn_shrink(a);
    bn_shrink(res);
    bn_t low = {};
    if (!bn_znew(&low, res->length))
        return false;
    bn_t high = {};
    if (!bn_znew(&high, a->length + res->length + 5))
        return false;
    bn_t sum = {};
    if (!bn_znew(&sum, a->length + res->length + 5))
        return false;
    for (int i = 0; i < a->length; i++) {
        for (int j = 0; j < 64; j += 32) {
            unsigned long long multiplier = ((a->num[i]) >> j) & 0xffffffffULL;
            bn_extend(&low, res->length);
            bn_move(res, &low);
            bn_extend(&high, res->length);
            bn_move(res, &high);
            bn_rshift(&high, 32);
            bn_mask(&low, 0xffffffffULL);
            bn_mask(&high, 0xffffffffULL);

            for (int k = 0; k < low.length; k++)
                low.num[k] *= multiplier;
            for (int k = 0; k < high.length; k++)
                high.num[k] *= multiplier;

            bn_lshift(&high, 32);
            bn_extend(&high, low.length + 1);
            bn_add_carry(&low, &high, 0);
            bn_lshift(&high, i * 64 + j);
            bn_extend(&sum, high.length + 1);
            bn_add_carry(&high, &sum, 0);
        }
    }
    bn_swap(&sum, res);
    bn_free(&sum);
    bn_free(&low);
    bn_free(&high);
    bn_shrink(res);
    return true;
}
/* swap a and b*/
void bn_swap(bn_t *a, bn_t *b)
{
    swap(a->length, b->length);
    swap(a->num, b->num);
}