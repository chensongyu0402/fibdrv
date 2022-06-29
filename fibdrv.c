#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include "bn.h"

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("Fibonacci engine driver");
MODULE_VERSION("0.1");

#define DEV_FIBONACCI_NAME "fibonacci"
#define DOUBLING

/* MAX_LENGTH is set to 92 because
 * ssize_t can't fit the number > 92
 */
#define MAX_LENGTH 92

static dev_t fib_dev = 0;
static struct cdev *fib_cdev;
static struct class *fib_class;
static DEFINE_MUTEX(fib_mutex);
/* obtain fibonacci sequence */
static long long fib_sequence(long long k, bn_t *ret)
{
    if (k == 0 || k == 1) {
        ret->num = kmalloc(sizeof(unsigned long long), GFP_KERNEL);
        ret->num[0] = k;
        ret->length = 1;
        return ret->length;
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

static long long fib_doubling(long long k, bn_t *ret)
{
    return 0;
}

static int fib_open(struct inode *inode, struct file *file)
{
    if (!mutex_trylock(&fib_mutex)) {
        printk(KERN_ALERT "fibdrv is in use");
        return -EBUSY;
    }
    return 0;
}

static int fib_release(struct inode *inode, struct file *file)
{
    mutex_unlock(&fib_mutex);
    return 0;
}

/* calculate the fibonacci number at given offset */
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

    if (res->size <= 0 || res->size > size) {
        printk("read error:res_size = %ld\n", sres_size);
        return 0;
    }
    access_ok(buf, size);
    k_to_out = ktime_get();
    if (copy_to_user(buf, res.num, res_size))
        res.size = 0;
    k_to_out = ktime_sub(ktime_get(), k_to_out);
    bn_free(&res);
    return res_size;
}

/* write operation is skipped */
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

static loff_t fib_device_lseek(struct file *file, loff_t offset, int orig)
{
    loff_t new_pos = 0;
    switch (orig) {
    case 0: /* SEEK_SET: */
        new_pos = offset;
        break;
    case 1: /* SEEK_CUR: */
        new_pos = file->f_pos + offset;
        break;
    case 2: /* SEEK_END: */
        new_pos = MAX_LENGTH - offset;
        break;
    }

    // if (new_pos > MAX_LENGTH)
    //     new_pos = MAX_LENGTH;  // max case
    if (new_pos < 0)
        new_pos = 0;        // min case
    file->f_pos = new_pos;  // This is what we'll use now
    return new_pos;
}

const struct file_operations fib_fops = {
    .owner = THIS_MODULE,
    .read = fib_read,
    .write = fib_write,
    .open = fib_open,
    .release = fib_release,
    .llseek = fib_device_lseek,
};

static int __init init_fib_dev(void)
{
    int rc = 0;

    mutex_init(&fib_mutex);

    // Let's register the device
    // This will dynamically allocate the major number
    rc = alloc_chrdev_region(&fib_dev, 0, 1, DEV_FIBONACCI_NAME);

    if (rc < 0) {
        printk(KERN_ALERT
               "Failed to register the fibonacci char device. rc = %i",
               rc);
        return rc;
    }

    fib_cdev = cdev_alloc();
    if (fib_cdev == NULL) {
        printk(KERN_ALERT "Failed to alloc cdev");
        rc = -1;
        goto failed_cdev;
    }
    fib_cdev->ops = &fib_fops;
    rc = cdev_add(fib_cdev, fib_dev, 1);

    if (rc < 0) {
        printk(KERN_ALERT "Failed to add cdev");
        rc = -2;
        goto failed_cdev;
    }

    fib_class = class_create(THIS_MODULE, DEV_FIBONACCI_NAME);

    if (!fib_class) {
        printk(KERN_ALERT "Failed to create device class");
        rc = -3;
        goto failed_class_create;
    }

    if (!device_create(fib_class, NULL, fib_dev, NULL, DEV_FIBONACCI_NAME)) {
        printk(KERN_ALERT "Failed to create device");
        rc = -4;
        goto failed_device_create;
    }
    return rc;
failed_device_create:
    class_destroy(fib_class);
failed_class_create:
    cdev_del(fib_cdev);
failed_cdev:
    unregister_chrdev_region(fib_dev, 1);
    return rc;
}

static void __exit exit_fib_dev(void)
{
    mutex_destroy(&fib_mutex);
    device_destroy(fib_class, fib_dev);
    class_destroy(fib_class);
    cdev_del(fib_cdev);
    unregister_chrdev_region(fib_dev, 1);
}

module_init(init_fib_dev);
module_exit(exit_fib_dev);
