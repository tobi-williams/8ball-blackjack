#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/prandom.h>

MODULE_LICENSE("GPL");

static int device_open(struct inode *inode, struct file *file);
static int device_close(struct inode *inode, struct file *file);
static ssize_t device_read(struct file *file, char __user *buff, size_t len, loff_t *offset);
static ssize_t device_write(struct file *file, const char __user *buff, size_t len, loff_t *offset);

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = device_open,
    .release = device_close,
    .read = device_read,
    .write = device_write
};

static struct miscdevice magic8ball = {
    .name = "magic8ball",
    .minor = MISC_DYNAMIC_MINOR,
    .fops = &fops,
    .mode = 0444
};

static const char *strings[] = {
    "It is certain.\n",
    "It is decidedly so.\n",
    "Without a doubt.\n",
    "Yes, definitely.\n",
    "You may rely on it.\n",
    "As I see it, yes.\n",
    "Most likely.\n",
    "Outlook good.\n",
    "Yes.\n",
    "Signs point to yes.\n",
    "Reply hazy, try again.\n",
    "Ask again later.\n",
    "Better not tell you now.\n",
    "Cannot predict now.\n",
    "Concentrate and ask again.\n",
    "Don't count on it.\n",
    "My reply is no.\n",
    "My sources say no.\n",
    "Outlook not so good.\n",
    "Very doubtful.\n"
};

static int device_open(struct inode *inode, struct file *file) {
    printk(KERN_INFO "Magic8Ball device opened\n");
    return 0;
}

static int device_close(struct inode *inode, struct file *file) {
    printk(KERN_INFO "Magic8Ball device closed\n");
    return 0;
}

static ssize_t device_read(struct file *file, char __user *buff, size_t len, loff_t *offset){
    unsigned int rand_num = prandom_u32(); //generate random number
    size_t str_len;
    
    if(*offset > 0){
    	return 0; //EOF
    }
    
    rand_num %= ARRAY_SIZE(strings); //truncate random number to bounds of array

    str_len = strlen(strings[rand_num]); //get length of chosen random string
    if((str_len) > len){ //make sure to not write more than length of provided buffer
        str_len = len;
    }

    if(copy_to_user(buff, strings[rand_num], str_len)){ //copy string to user space buffer, check for error
        return -EFAULT;
    }

    *offset += str_len;
    return str_len;
}

static ssize_t device_write(struct file *file, const char __user *buff, size_t len, loff_t *offset){
    return -EPERM;
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
    misc_deregister(&magic8ball);
    printk(KERN_ALERT "Magic8Ball module unloaded\n");
}

module_init(magic8ball_init);
module_exit(magic8ball_exit);
