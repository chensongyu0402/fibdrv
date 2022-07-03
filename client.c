#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define FIB_DEV "/dev/fibonacci"
#define RUNS 50
#define OFFSET 1000

int bn_to_string(unsigned long long *bn, int bn_len, char **str_ptr);
void reverse(char *str, int len);

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
    int k_to_ut[OFFSET][RUNs] = {};
    unsigned long total[OFFSET][RUNS] = {};
    FILE *kt_fp = fopen("ktime.txt", "w");
    FILE *k_to_ut_fp = fopen("k_to_utime.txt", "w");
    FILE *ut_fp = fopen("utime.txt", "w");
    FILE *total_fp = fopen("total_time.txt", "w");
    /* calculate tine consumption*/
    for (int j = 0; j < RUNS; j++) {
        for (int i = 0; i < OFFSET; i++) {
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
    for (int i = 0; i <= offset; i++) {
        for (int j = 0; j < RUNS; j++) {
            fprintf(ut_fp, "%lu", ut[i][j]);
            fprintf(kt_fp, "%d", kt[i][j]);
            fprintf(k_to_ut_fp, "%d", k_to_ut[i][j]);
            fprintf(total_fp, "%lu", total[i][j]);
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
/* reverse binaray */
void reverse(char *str, int len)
{
    int half = len / 2;
    for (int i = 0; i < half; i++) {
        char tmp = str[i];
        str[i] = str[len - 1 - i];
        str[len - 1 - i] = tmp;
    }
}
