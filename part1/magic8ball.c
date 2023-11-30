#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/miscdevice.h>

MODULE_LICENSE("GPL");

static int device_open(struct inode *inode, struct file *file);
static int device_close(struct inode *inode, struct file *file);

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = device_open,
    .release = device_close
};

static struct miscdevice magic8ball = {
    .name = "magic8ball",
    .minor = MISC_DYNAMIC_MINOR,
    .fops = &fops,
    .mode = 0444
};

static const char *strings[] = {
    "It is certain.",
    "It is decidedly so.",
    "Without a doubt.",
    "Yes, definitely.",
    "You may rely on it.",
    "As I see it, yes.",
    "Most likely.",
    "Outlook good.",
    "Yes.",
    "Signs point to yes.",
    "Reply hazy, try again.",
    "Ask again later.",
    "Better not tell you now.",
    "Cannot predict now.",
    "Concentrate and ask again.",
    "Don't count on it.",
    "My reply is no.",
    "My sources say no.",
    "Outlook not so good.",
    "Very doubtful."
};

static int device_open(struct inode *inode, struct file *file) {
    printk(KERN_INFO "Magic8Ball device opened\n");
    return 0;
}

static int device_close(struct inode *inode, struct file *file) {
    printk(KERN_INFO "Magic8Ball device closed\n");
    return 0;
}

static int __init magic8ball_init(void) {
    int ret = misc_register(&magic8ball);
    if (ret < 0){
        printk(KERN_ERR "Magic8Ball module failed to load\n");
        return ret;
    }
    printk(KERN_ALERT "Magic8Ball module loaded successfully\n");
    return 0;
} 

static void __exit magic8ball_exit(void) {
    int ret = misc_deregister(&magic8ball);
    if(ret < 0){
        printk(KERN_ERR "Magic8Ball module failed to unload\n");
        return ret;
    }
    printk(KERN_ALERT "Magic8Ball module unloaded successfully\n");
    return 0;
}

module_init(magic8ball_init);
module_exit(magic8ball_exit);