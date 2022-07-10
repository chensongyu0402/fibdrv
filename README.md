# Implementing high performance Fibonacci sequence in Character Device Driver
###### tags: `github_project`
## Introduce
- 設計一個 character device, 透過定義相關的函數，可利用存取檔案的系統呼叫以存取。在使用者層級 (user-level 或 userspace) 中透過 read 系統呼叫來得到輸出。
- 利用新的 Data Structure 解決大數運算的問題
- 利用 bitwise 操作 以及 Fast Doubling Algorithm 改善 Fibonacci 運算的效能
## 開發環境
```bash
$ gcc --version
gcc (Ubuntu 9.4.0-1ubuntu1~20.04.1) 9.4.0

$ lscpu
架構：                          x86_64
CPU 作業模式：                   32-bit, 64-bit
Byte Order:                    Little Endian
Address sizes:                 39 bits physical, 48 bits virtual
CPU(s):                        8
On-line CPU(s) list:           0-7
每核心執行緒數：                  2
每通訊端核心數：                  4
Socket(s):                     1
NUMA 節點：                     1
供應商識別號：                   GenuineIntel
CPU 家族：                      6
型號：                          142
Model name:                    Intel(R) Core(TM) i5-8265U CPU @ 1.60GHz
製程：                          12
CPU MHz：                      1800.000
CPU max MHz:                   3900.0000
CPU min MHz:                   400.0000
BogoMIPS:                      3600.00
虛擬：                          VT-x
L1d 快取：                      128 KiB
L1i 快取：                      128 KiB
L2 快取：                       1 MiB
L3 快取：                       6 MiB
NUMA node0 CPU(s)：            0-7
```
## binary.c
### big number arithmetic
- 從原本的十進位改為二進位運算
  * 避免空間的浪費
- 使用 unsigned long long array(num)
  * 解決大數運算限制的問題
- 採用新的資料結構進行運算
  * ![](https://i.imgur.com/4QGg72C.jpg)
  * 每一段都是 64 bits 為單位，所以內容物最大為 2^64 - 1, 且只有進位的時候才會影響到彼此，不然通常是不互相影響的
  * 從右到左為(低位到高位) num[0], num[1], num[2] 
```c
typedef struct _bn {
    unsigned long long length;
    unsigned long long *num;
} bn_t;
```
### kmalloc, vmalloc, malloc
- allocate memory
  * kmalloc and vmalloc 在 kernel 中
  * malloc 在 user space 中
- kmalloc vs vmalloc
  * kmalloc： 會配置 continuous physical address, 所以適合用在 DMA, 且通常用來配置小於 page size 的 object
  * vmalloc: 會配置 continuous virtual address, 較適合用來配置需要很多 pages 的 object
- 底下透過 function 來配置的好處是可以方便我們更改我們的 code, 例如：kvmalloc, vmalloc. 我們只需要更改 function 內的 kmalloc 即可，不需要做過多的修改
### new
- bn_new and bn_znew
- 只對 bn_t 中 member num 配置記憶體，可以增加彈性程度
  * 想要配置多少長度給 member num 就配置多少
- bn_new 為正常的配置記憶體，而 bn_znew 則配置內容物為 0 的記憶體
```c
bool bn_new(bn_t *bn_ptr, unsigned long long length)
{
    bn_ptr->length = 0;
    if ((bn_ptr->num =
             kmalloc(sizeof(unsigned long long) * length, GFP_KERNEL)))
        bn_ptr->length = length;
    return !!bn_ptr->num;
}

bool bn_znew(bn_t *bn_ptr, unsigned long long length)
{
    bn_ptr->length = 0;
    if ((bn_ptr->num =
             kzalloc(sizeof(unsigned long long) * length, GFP_KERNEL)))
        bn_ptr->length = length;
    return !!bn_ptr->num;
}
```
### extend and shrink
- bn_zrenew, bn_extend, bn_shrink
- void *krealloc(const void *p, size_t new_size, gfp_t flags)
  * 為 p 重新申請一段 memory, 再將 p 之前 memory 中的内容複製過來
- 藉由 krealloc, 可以擴大(extend) member num 的記憶體配置
  * 可以增加配置上的彈性
  * 由於可能會遇到突然需要配置更多的記憶體，例如： 做完運算結果 overflow ，那可能就要先特過這些 function, 先讓 member num and length 增大，以避免遇到這種問題
- if (tmp == NULL)return false;
  * 可以用來避免 tmp 配置時所發生的錯誤，例如：配置失敗
- bn_zrenew
  * 會配置更多的 0
- bn_extend
  * 會保留原本的內容，其餘想要多配置的記憶體的內容以 0 取代
- bn_shrink
  * 會刪除過多的 0, 避免在後面計算 bn_mult 做不必要的運算，例如：一堆 0
  * 因此它會從最高位往下慢慢 scan 直到不是 num[i] > 0 為止
```c
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
```
### free
- bn_free
- 透過 kfree 可以釋放在 kernel 中的記憶體
```c
void bn_free(bn_t *bn_ptr)
{
    kfree(bn_ptr->num);
    bn_ptr->length = 0;
    bn_ptr->num = NULL;
}
```
### swap
- bn_swa
- 將兩個 bn_t 調換
  * 主要是可以幫助 Fibonacci 運算
  * 例如： a3 = a1 + a2
  * 下次就必須讓 a2 變成 a1, 以及 a3 變成 a2, 因此需要 bn_swap 幫忙
```c
void bn_swap(bn_t *a, bn_t *b)
{
    swap(a->length, b->length);
    swap(a->num, b->num);
}
```
### move
- bn_move and bn_toggle_move
- bn_move
  * 主要用於搬移 bn_t
  * 多的地方記得要補 0
- bn_toggle_move
  * 主要是用在使用 bn_sub
  * 計算 1’s complement 並搬移到 res
```c
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
```
### shift and mask
- bn_lshift, bn_rshift, bn_mask
- bn_lshift
  * shift_len 是在計算移動多少個 length(以 64 bits 為單位)，所以要 " >>3 再 /sizeof(shift_len)"，最後除不盡還是要算，因此還是要 "+1"
  * mod_bits 是限制移動的 bits 數(或是可以看成餘數的概念)，不超過 64 bits, 所以才需要 "bits & 0x3fULL"
  * remained 主要是可以判斷 res 被 shift 的長度夠不夠，如過不夠可以透過 bn_extend 增加長度
    * shift_len 比 res->length 大
    * 也需要判斷 shift 過後會不會 overflow, 所以必須從 res->length - shift_len 開始 scan
  * 透過 && 的使用，如果 remained 為 false 才會使用到 bn_extend ，並確認 extend 過程中有錯誤
  * 再來必須考慮 left shift 在新的 number arithmetic 下的移動方式
    * 首先一定是從最高位開始計算
    * 再來必須考慮可能會被移動到此位置的 member num, 因此必須考慮 "i - shift_len + 1" and "i - shift_len" ，因為他們是最有可能會移動到 i 這個位置的
    * 先取得 "i - shift_len + 1"<< mod_bits 是因為這個一定是被 left shift 的地方，再取 "i - shift_len" >> (63 - mod_bits)) >> 1 也是只被 left  shift 的部份，最後再合併透過 ｜ 的方式
    * 重複做上面的動作後，"shift_len - 1" 必須也要被計算，原理同上，只是只要計算 num[0] << mod_bits 即可
    * 最後剩餘的部份補 0 即完成
- bn_rshift
  * 原理其實跟 lshift 類似
  * shift_len 以及 mod_bits 的功能與 lshift 一樣
  * 特別的是 shift_len > res->length 代表全部被 right shift 就沒有留下的必要，所以最後直接 memset 讓 res 內容為 0，並改掉長度，避免浪費。
  * 再來也必須考慮 right shift 在新的 number arithmetic 下的移動方式
    * 首先一定是從最低位開始計算
    * 先取得一定被 right shift 移動到的地方，分別是 " i + shift_len" and "i + shift_len + 1"
    * 再將 "i + shift_len" >> mod_bits, 因為這個地方是一定會被 right shift, "i + shift_len + 1" << (63 - mod_bits)) << 1 代表我們只要取得他被 rught shift 的部份，最後在透過 ｜ 合併
    * 重複做上面的動作後，" res->length - shift_len - 1 " 必須也要被計算，原理同上，只是只要計算 num[res->length - 1] << mod_bits 即可
    * 最後剩餘的部份補 0, 同時也不需要那麼長的長度需求，所以 res->length - shift_len
- bn_mask
  * 可以當作遮罩，遮住 member num 特定的 bits
  * num array 中 每個都做 mask
```c
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
                      ((res->num[i - shift_len] >> (63 - mod_bits)) >> 1);
    res->num[shift_len - 1] = res->num[0] << mod_bits;
    for (int i = 0; i < shift_len - 1; i++)
        res->num[i] = 0;
    return true;
}

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
        res->num[i] = (res->num[i + shift_len] >> mod_bits) |
                      ((res->num[i + shift_len + 1] << (63 - mod_bits)) << 1);
    res->num[res->length - shift_len - 1] =
        res->num[res->length - 1] >> mod_bits;
    for (i = res->length - shift_len; i < res->length - 1; i++)
        res->num[i] = 0;
    res->length = res->length - shift_len;
    return;
}

void bn_mask(bn_t *ptr, unsigned long long msk)
{
    for (int i = 0; i < ptr->length; i++)
        ptr->num[i] &= msk;
}
```
### arithmetic operation
- bn_add, bn_sub, bn_add_carry, bn_mult
- bn_add_carry
  * 在說明其他三個以前，必須先說明這個 function
  * ![](https://i.imgur.com/po3SJTv.png)
  * 採用 bit operation 判斷 overflow, 只要 overflow 發生就會使得下一步的 carry + 1
  * 主要發生 carry 會發生在上面四種情形，可以不難判斷就是 carry 與 overflow and msb 間有關係
  * i < b->length：
    * overflow = b & res
    * msb = b | res, 主要是判斷紀錄目前最大位數
    * b and carry 加到 res 後，確認 res 的 msb 是 0 還是 1, 可以判斷到底有沒有進位的發生，因此才需要用 "~res"
    * overflow | msb 同時考慮
    * 可以透過看最高位來判斷須不需要 carry
  * i >= b->length && i < res->length
    * 剩下沒算到的部份，因為 b 已經加完了，在加法運算的過程只需要考慮 carry 即可 
    * 在 for loop 內 "&& carry" 可以避免浪費計算資源，白話點就是沒有 carry 就不用再算了
    * 這裡不用考慮 overflow 是因為沒有 b 不用加 res 了。
    * msb 一樣可以判斷有沒有進位的發生
    * 所以加完 carry 後 ==> "~res"
    * 最後確認最高位判斷須不需要 carry
- bn_add
  * 為了讓 a 確定是相對大的(與 bn_add_carry 設計有關)，所以必須判斷 a and b 的大小關係，最後如果 b 比較大，就 swap(a,b)
  * 為了避免 res 最後加完 overflow, 所以根據現在最大的 a 來幫助調整 res ，如果不夠的話， extend res, 讓 res->length = a->length + 1
  * 最後就可以運算了
- bn_sub
  * 避免 res 長度不足，因此會根據目前最大的 length 配置
  * 由於在 fibonacci 計算當中，不會出現"小減大"的情形，因此這裡只考慮 "a->length > b->length" or "a->length == b->length", 這也是這個 function 的缺點，如果以後要做的完整(為了其他特別的運算)就必須多考慮"小減大"
  * a->length > b->length:
    * 由於 bn_add_carry 的設計就是要讓 res 會儲存相對大的值，所以運算會相對複雜
    * 首先 res = -a - 1, 其實就是 a 的 1's complement
    * res = res + b = -a - 1 + b = b - a - 1
    * res =  - res - 1 = - (b - a- 1) - 1 = - b + a = a - b
  * a->length = b->length:
    * res = - b - 1
    * res = res + a + 1(carry) = - b - 1 + a + 1 = a - b
- bn_mult
  * bn_shrink 用於縮減 a and res, 可以避免不必要的計算
  * low and high 分別是用來紀錄較低的 32 bits 以及較高的 32 bits
    * 分為 low and high, 可以避免再去想一個新的 overflow and carry 的處理方式
    * low 因為不需要那麼多長度，所以只要配置 res->length
    * high 必須去計算 carry 且又屬於較高 32 bits, 所以要配置多一點甚至是多給他。因此配置了 res->length + a->length + 5
  * sum 用於累計目前的總和，因此也是要配置 res->length + a->length + 5 的記憶體
  * 從最小的 num index 開始計算，且 multiplier 每次只處理 32 bits 的計算，主要是為了配合 low and high
  * 在計算前還是要確保 low and high >= 特定長度
  * 取得完 low and high, 即可以直接乘以 multiplier
  * 將計算好的 high 移回去( << 32 ), 並確保長度只少大於等於 low.length + 1, 可以讓 bn_add_carry 計算
  * 計算完會都加到 high 上，由於隨著 multiplier 不斷的變大(num index 增加)，所以 high 實質上的大小也應該要變得更大，因此透過 bn_lshift 可以知道目前要移動多少 bit(i * 64 + j)
  * extend sum 的理由同 extend high, 最後累計 high 到 sum 裡面，不斷重複上面的事情，即可得到最後的答案(sum)
  * 最後 move sum to res, free 過程中暫時配置的記憶體，並 shrink res, 減少之後的計算資源
```c
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
    for (; i < res->length && carry; i++) {
        unsigned long long msb = res->num[i];
        res->num[i] += carry;
        msb &= ~res->num[i];
        carry = !!(msb & 1ULL << 63);
    }
}

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
```
## fibdrv.c
### fibonacci sequence
- 此為最經典的 DP 方法
- a, b, res 分別紀律在 for loop 中第一個和第二個以及第三個
- err 主要是確認過程中有沒有配置或是計算錯誤，如果有誤的話，最後就 free(ret)
- for loop 中 bn_swap 可以不斷更新 a and b
```c
static long long fib_sequence(long long k, bn_t *ret)
{
    if (k == 0 || k == 1) {
        ret->num = kmalloc(sizeof(unsigned long long), GFP_KERNEL);
        ret->num[0] = k;
        ret->length = 1;
        return 1;
    }

    bn_t a, b, res = {};
    bn_znew(&a, 1);
    bn_znew(&b, 1);

    if (!a.num || !b.num) {
        bn_free(&a);
        bn_free(&b);
        return 0;
    }
    /* caculate fib*/
    a.num[0] = 0;
    b.num[0] = 1;
    bool err = false;
    for (int i = 2; i <= k; i++) {
        if (!bn_add(&a, &b, &res)) {
            err = true;
            break;
        }
        bn_swap(&a, &b);
        bn_swap(&b, &res);
    }
    /* free memory and check no error*/
    bn_free(&a);
    bn_free(&res);
    bn_swap(ret, &b);
    if (err) {
        bn_free(ret);
        return 0;
    }
    return ret->length;
}
```
### fibonacci doubling
- 此為改善 fibonacci 另一個方法，更加快速
- $$
            \begin{split}
            F(2k) &= F(k)[2F(k+1) - F(k)]\\
            F(2k+1) &= F(k+1)^2+F(k)^2
            \end{split}
  $$
  * k 值為我們要取得第幾個 fibonacci
  * builtin_clz 可以快速計算有多少 leading 0s, 可以直接避免從 0 開始 scan 浪費許多計算資源
  * 我們會根據 k 之 binary representation, 從高位往下 scan 並根據 bits 做相對應的運算
    * 遇到 `0` $\rightarrow$ 進行 fast doubling，也就是求 $F(2n)$ 和 $F(2n+1)$
    * 遇到 `1` $\rightarrow$ 進行 fast doubling，也就是先求 $F(2n)$ 和 $F(2n+1)$，再求 $F(2n+2)$
- ![](https://i.imgur.com/iFaIMX8.png)
- 接下來就是透過已經建立好的 function 透過一系列的計算實作出 fast doubling
- err 的使用方式跟前面一樣，這裡也可以看出之所以用大量的 bool function, 就是為了確認過程中的配置或是計算錯誤
```c
static long long fib_doubling(long long k, bn_t *ret)
{
    if (k == 0 || k == 1) {
        ret->num = kmalloc(sizeof(unsigned long long), GFP_KERNEL);
        ret->length = 1;
        ret->num[0] = k;
        return 1;
    }

    bn_t a, b;
    bn_znew(&a, 2);
    bn_znew(&b, 2);
    int bits = 32 - __builtin_clz(k);
    if (!a.num || !b.num) {
        bn_free(&a);
        bn_free(&b);
        return 0;
    }
    a.num[0] = 0;
    b.num[0] = 1;
    bool err = false;
    for (int i = bits - 1; i >= 0; i--) {
        bn_t t1 = {}, t2 = {}, t3 = {}, t4 = {};
        err |= !bn_new(&t1, b.length);
        err |= !bn_move(&b, &t1);
        err |= !bn_lshift(&t1, 1);     // t1 = 2*b
        err |= !bn_sub(&t1, &a, &t2);  // t1 = 2*b - a
        err |= !bn_new(&t3, a.length);
        err |= !bn_move(&a, &t3);   // t3 = a
        err |= !bn_mult(&t3, &t2);  // t2 = a(2*b - a)

        err |= !bn_mult(&a, &t3);
        err |= !bn_move(&b, &t1);
        err |= !bn_mult(&b, &t1);
        err |= !bn_add(&t1, &t3, &t4);  // t4 = a^2 + b^2

        err |= !bn_extend(&a, t2.length);
        err |= !bn_extend(&b, t4.length);
        err |= !bn_move(&t2, &a);
        err |= !bn_move(&t4, &b);

        if (k & 1 << i) {
            err |= !bn_add(&a, &b, &t1);  // t1 = a+b
            err |= !bn_extend(&a, b.length);
            err |= !bn_move(&b, &a);  // a = b
            err |= !bn_extend(&b, t1.length);
            err |= !bn_move(&t1, &b);  // b = t1
        }

        bn_free(&t1);
        bn_free(&t2);
        bn_free(&t3);
        bn_free(&t4);
        if (err)
            break;
    }
    bn_swap(&a, ret);
    bn_free(&a);
    bn_free(&b);
    if (err) {
        bn_free(ret);
        return 0;
    }
    return ret->length;
}
```
### read
- res_size 會紀錄 fibonacci 運算 得到的 bytes 數
- 計算 fib_doubling or fib_sequence, 再將內容 copy to user. 整個過程中還會計算 kernel time 以及 copy_to_user time
- 透過 #ifdef and #endif 來確認是否有定義 DOUBLING, 以決定是否採用 fib_doubling function
- 使用 ktime_get and ktime_sub 可以算出 fib_doubling 時間花費(kernel time)
- access_ok 為 user pointer 檢查，確保系統調用傳給 kernel 的 pointer 是 user space 而非 kernel space 的地址，如果是 kernel space 的地址，到時候使用 copy to user 時，就會有安全上面的問題
- copy to user 將內容複製到 user buffer 內，讓 client.c 可以取得內容
```c
static ssize_t fib_read(struct file *file,
                        char *buf,
                        size_t size,
                        loff_t *offset)
{
    bn_t res = {};
    bool doubling = false;
#ifdef DOUBLING
    doubling = true;
#endif
    kt = ktime_get();
    ssize_t res_size;
    if (doubling)
        res_size = fib_doubling(*offset, &res) * sizeof(unsigned long long);
    else
        res_size = fib_sequence(*offset, &res) * sizeof(unsigned long long);
    kt = ktime_sub(ktime_get(), kt);

    if (res_size <= 0 || res_size > size) {
        printk("read error:res_size = %ld\n", res_size);
        return 0;
    }
    access_ok(buf, size);
    k_to_ut = ktime_get();
    if (copy_to_user(buf, res.num, res_size))
        res_size = 0;
    k_to_ut = ktime_sub(ktime_get(), k_to_ut);
    bn_free(&res);
    return res_size;
}
```
### write
- 透過 offset 的設定，並使用 ktime_to_ns function 可以取得花費的實際時間(ns)
```c
static ssize_t fib_write(struct file *file,
                         const char *buf,
                         size_t size,
                         loff_t *offset)
{
    if (*offset == 0)
        return ktime_to_ns(kt);
    else if (*offset == 1)
        return ktime_to_ns(k_to_ut);
    return 0;
}
```
## client.c
### reverse
- 對 string 做 reverse
- 要記住是 char *str
```c
void reverse(char *str, int len)
{
    int half = len / 2;
    for (int i = 0; i < half; i++) {
        char tmp = str[i];
        str[i] = str[len - 1 - i];
        str[len - 1 - i] = tmp;
    }
}
```
### bn_to_string
- 用於將 binary representation transfer to string
- width 為 member num 的單位長度
- bn_len 為  member num 長度
- bn_bits 為 member num 總 bits 數
- len 則是 10 進為表示長度
  * 10 進為表達 member num 所需要的長度
  * log2 to log10
  * len = bn_bits / 3 + 2 這是一個近似的作法
- total_len 會記錄實際的長度
- str and str_ptr 都是用來存 string
- 從最高位開始往下處理，每次處理 32 bits
- 重點在於第三個 for loop
  * k < len 因為 len 是最大可能的值
  * k < len 才有可能是 1 才會繼續執行下去 &&
  * 如果 || 左邊是 1 ，右邊就不會繼續執行
    * 代表著目前 total_length 足夠的
  * 如果 || 左邊是 0 ，右邊就會執行
    * 會先去確認 carry, 有 carry 才需要增加 total_length, 也就是 carry > 0, && 後面才能執行，也可以避免計算資源浪費
    * totoal_length 就必須加一
- 每次處理都是 << 32, 原因就是前面是從高位元 32 bits 往下處理
- 最後透過 +'0' 轉為 string
- 因為是從高位元處理並儲存在 str 的低位，所以最後必須再做 reverse
```c
int bn_to_string(unsigned long long *bn, int bn_len, char **str_ptr)
{
    int width = sizeof(*bn) * 8;
    int bn_bits = bn_len * width;
    int len = bn_bits / 3 + 2;  // log2 to log10
    int total_len = 1;
    unsigned char *str = calloc(len, 1);
    if (!str)
        return 0;

    for (int i = bn_len - 1; i >= 0; i--) {
        for (int j = width - 32; j >= 0; j -= 32) {
            unsigned long long carry = bn[i] >> j & 0xffffffffllu;
            for (int k = 0; k < len && (k < total_len || carry && ++total_len);
                 k++) {
                carry += (unsigned long long) str[k] << 32;
                str[k] = carry % 10;
                carry /= 10;
            }
        }
    }

    for (int k = 0; k < total_len; k++)
        str[k] += '0';

    reverse(str, total_len);
    *str_ptr = str;
    return len;
}
```
### main
- RUNS 主要是為了可以跑很多次，避免 outlier 的出現
- sz 為 read 中的 res_size, 因此會紀錄 fibonacci 運算 的 bytes 數
- buf 存放 fibonacci 運算得到的內容
- write_buf 主要是用在前面的測試(testing)，確保 driver 沒有問題
- fd 儲存 file open 值
- kt, ut, k_to_ut, total 分別為 kernel time, user time, copy to user time, total time. 也有他們對應的 txt file, 可以儲存過程中得到的時間資料
- 透過 lseek 與 read 可以得到想要的 fibonacci
  * lseek 可以調整 offset, read 就可以根據 offset 得到對應的  fibonacci number
- tp_start, tp_end 透過 clock_gettime 可以計算 binary_to_string 花費時間(user time)
- 再透過 lseek and write 可以得到 kernel time and copy to user time
```c
int main()
{
    long long sz;

    unsigned long long buf[500];
    char write_buf[] = "testing writing";
    int offset = OFFSET; /* TODO: try test something bigger than the limit */

    int fd = open(FIB_DEV, O_RDWR);
    if (fd < 0) {
        perror("Failed to open character device");
        exit(1);
    }
    /* testing */
    for (int i = 0; i <= offset; i++) {
        sz = write(fd, write_buf, strlen(write_buf));
        printf("Writing to " FIB_DEV ", returned the sequence %lld\n", sz);
    }
    /* running */
    int kt[OFFSET][RUNS] = {};
    unsigned long ut[OFFSET][RUNS] = {};
    int k_to_ut[OFFSET][RUNS] = {};
    unsigned long total[OFFSET][RUNS] = {};
    FILE *kt_fp = fopen("ktime.txt", "w");
    FILE *k_to_ut_fp = fopen("k_to_utime.txt", "w");
    FILE *ut_fp = fopen("utime.txt", "w");
    FILE *total_fp = fopen("total_time.txt", "w");
    /* calculate tine consumption*/
    for (int j = 0; j < RUNS; j++) {
        for (int i = 0; i < offset; i++) {
            lseek(fd, i, SEEK_SET);
            sz = read(fd, buf, 200 * sizeof(unsigned long long));
            char *str;
            struct timespec tp_start, tp_end;
            clock_gettime(CLOCK_MONOTONIC, &tp_start);
            bn_to_string(buf, sz / sizeof(*buf), &str);
            clock_gettime(CLOCK_MONOTONIC, &tp_end);
            ut[i][j] = tp_end.tv_nsec - tp_start.tv_nsec;
            printf("Reading from " FIB_DEV
                   " at offset %d, reutrned the sequence "
                   "%s. \n",
                   i, str);
            free(str);

            lseek(fd, 0, SEEK_SET);
            kt[i][j] = write(fd, write_buf, strlen(write_buf));
            lseek(fd, 1, SEEK_SET);
            k_to_ut[i][j] = write(fd, write_buf, strlen(write_buf));
            total[i][j] = ut[i][j] + kt[i][j] + k_to_ut[i][j];
        }
    }
    /* store data */
    for (int i = 0; i < offset; i++) {
        for (int j = 0; j < RUNS; j++) {
            fprintf(ut_fp, "%lu ", ut[i][j]);
            fprintf(kt_fp, "%d ", kt[i][j]);
            fprintf(k_to_ut_fp, "%d ", k_to_ut[i][j]);
            fprintf(total_fp, "%lu ", total[i][j]);
        }
        fprintf(ut_fp, "\n");
        fprintf(kt_fp, "\n");
        fprintf(k_to_ut_fp, "\n");
        fprintf(total_fp, "\n");
    }
    fclose(ut_fp);
    fclose(kt_fp);
    fclose(k_to_ut_fp);
    fclose(total_fp);
    /* printf fid sequence */
    for (int i = offset; i >= 0; i--) {
        lseek(fd, i, SEEK_SET);
        sz = read(fd, buf, 200 * sizeof(unsigned long long));
        char *str;
        bn_to_string(buf, sz / sizeof(*buf), &str);
        printf("Reading from " FIB_DEV
               " at offset %d, returned the sequence "
               "%s.\n",
               i, str);
        free(str);
    }

    close(fd);
    return 0;

    close(fd);
    return 0;
}
```
## performance
### outlier and plot
- 透過 outlier function 可以去除掉過大或是過小的值，以確保整個實驗的準確性
- datas 的資料為計算某個 fibonacci number 的時間，並用此手法去除 95% 區間之外的值
  * ![](https://i.imgur.com/WLomShd.png)
```python
import numpy as np
import math
import matplotlib.pyplot as plt

def outlier_filter(datas, threshold = 2):
    datas = np.array(datas).astype(np.uint)
    if (math.isclose(0, datas.std(), rel_tol = 1e-10)):
        return datas
    z = np.abs((datas - datas.mean()) / datas.std())
    return datas[z < threshold]

def data_processing(datas):
    for i in range(datas.shape[0]-1): 
        if i == 0:
            final_arr = outlier_filter(datas[i]).mean()
        else:
            final_arr = np.append(final_arr,outlier_filter(datas[i]).mean())
    return final_arr

if __name__ == "__main__":
    # txt to numpy array and detect outlier
    klines = []
    ulines = []
    k2ulines = []
    totallines = []
    with open("ktime.txt") as textFile:
        klines = [line.split() for line in textFile]
    with open("utime.txt") as textFile:
        ulines = [line.split() for line in textFile]
    with open("k_to_utime.txt") as textFile:
        k2ulines = [line.split() for line in textFile]
    with open("total_time.txt") as textFile:
        totallines = [line.split() for line in textFile]
    klines = data_processing(np.array(klines))
    ulines = data_processing(np.array(ulines))
    k2ulines = data_processing(np.array(k2ulines))
    totallines =  data_processing(np.array(totallines))
    num = np.array([x for x in range(1,1000)])
    # plot
    plt.title("execution time")
    plt.xlabel("n")
    plt.ylabel("ns")
    plt.plot(num, klines)
    plt.plot(num, k2ulines)
    plt.plot(num, ulines)
    plt.plot(num, totallines)
    plt.legend(['kernel_time', 'copy to user time', 'user time', 'total_time'])
    plt.show()
    pngName = "execution_time.png"
    plt.savefig(pngName)
    plt.close()

```
### 結論
- befor
  * ![](https://i.imgur.com/3eFtZOs.png)
- after
  * ![](https://i.imgur.com/OkLRMhi.png)
- 可以看到 doubling algorithm 效果明顯較好
- 無論是 before 還是 after 都比正常的都來的低
  * 大量的使用 bits 的操作
  * bits 的表示法，而不是 10 進位
  * 透過硬體加速的方式，也讓效能更好，例如：clz / ctz
- 當然不止於現在這些方法，應該還是有辦法更低，但目前是沒有想到的