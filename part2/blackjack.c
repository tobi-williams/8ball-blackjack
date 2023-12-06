#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/prandom.h>
#include <linux/string.h>

MODULE_LICENSE("GPL");

static int device_open(struct inode *inode, struct file *file);
static int device_close(struct inode *inode, struct file *file);
static ssize_t device_read(struct file *file, char __user *buff, size_t len, loff_t *offset);
static ssize_t device_write(struct file *file, const char __user *buff, size_t len, loff_t *offset);
static void shuffle(void);
static void reset(void);
static int write_msg(char msg[]);

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = device_open,
    .release = device_close,
    .read = device_read,
    .write = device_write
};

static struct miscdevice blackjack = {
    .name = "blackjack",
    .minor = MISC_DYNAMIC_MINOR,
    .fops = &fops,
    .mode = 0666
};


static char *card_deck[] = {
	"Ace of Spades\n",
	"2 of Spades\n",
	"3 of Spades\n",
	"4 of Spades\n",
	"5 of Spades\n",
	"6 of Spades\n",
	"7 of Spades\n",
	"8 of Spades\n",
	"9 of Spades\n",
	"10 of Spades\n",
	"Jack of Spades\n",
	"Queen of Spades\n",
	"King of Spades\n",
	"Ace of Hearts\n",
	"2 of Hearts\n",
	"3 of Hearts\n",
	"4 of Hearts\n",
	"5 of Hearts\n",
	"6 of Hearts\n",
	"7 of Hearts\n",
	"8 of Hearts\n",
	"9 of Hearts\n",
	"10 of Hearts\n",
	"Jack of Hearts\n",
	"Queen of Hearts\n",
	"King of Hearts\n",
	"Ace of Diamonds\n",
	"2 of Diamonds\n",
	"3 of Diamonds\n",
	"4 of Diamonds\n",
	"5 of Diamonds\n",
	"6 of Diamonds\n",
	"7 of Diamonds\n",
	"8 of Diamonds\n",
	"9 of Diamonds\n",
	"10 of Diamonds\n",
	"Jack of Diamonds\n",
	"Queen of Diamonds\n",
	"King of Diamonds\n",
	"Ace of Clubs\n",
	"2 of Clubs\n",
	"3 of Clubs\n",
	"4 of Clubs\n",
	"5 of Clubs\n",
	"6 of Clubs\n",
	"7 of Clubs\n",
	"8 of Clubs\n",
	"9 of Clubs\n",
	"10 of Clubs\n",
	"Jack of Clubs\n",
	"Queen of Clubs\n",
	"King of Clubs\n"
};

static int card_numbers[] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
	14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
	27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 29,
	40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52
};

enum state {
	DISABLED = 0,
	RESET = 1,
	SHUFFLED = 2,
};

struct game_state {
    enum state current_state;
    int dealer_score;
    int player_score;
};

static char msg_buffer[1024] = {0};
static struct game_state current_game;
static DEFINE_MUTEX(blackjack_mutex);


static int device_open(struct inode *inode, struct file *file) {
    printk(KERN_INFO "Blackjack device opened\n");
    return 0;
}

static int device_close(struct inode *inode, struct file *file) {
    printk(KERN_INFO "Blackjack device closed\n");
    return 0;
}

static ssize_t device_read(struct file *file, char __user *buff, size_t len, loff_t *offset){
    //return -EPERM;
    size_t bytes_to_copy;
    
    /*if(*offset >= strlen(msg_buffer)){
    	return 0;
    }*/
    
    if (len >= strlen(msg_buffer)){
    	bytes_to_copy = strlen(msg_buffer);
    } else{
    	bytes_to_copy = len;
    }
    
    if(copy_to_user(buff, msg_buffer, bytes_to_copy)){
    	return -EFAULT;
    }
    
    //*offset += bytes_to_copy;
    memset(msg_buffer, 0, 1024);
    
    return bytes_to_copy;
}

static ssize_t device_write(struct file *file, const char __user *buff, size_t len, loff_t *offset){
	char command[256];

	if (len > (sizeof(command) - 1)){
		return -EINVAL;
	}
	command[len] = '\0';

	if (copy_from_user(command, buff, len)){
		return -EFAULT;
	}

	if (strncasecmp(command, "RESET", 5) == 0){
		reset();
		current_game.current_state = 1;
		
		if (write_msg("RESET")){
			return -ENOSPC;
		}
	}
	
	if (strncasecmp(command, "SHUFFLE", 7) == 0){
		
		if (current_game.current_state == 0){
			
			if (write_msg("INVALID STATE")){
				return -ENOSPC;
			}
		} 
		else{
			shuffle();
			current_game.current_state = 2;
			
			if (write_msg("SHUFFLE")){
				return -ENOSPC;
			}
		}
	}
	
	return len;
}

static void shuffle(){
    size_t i, j;
    int tmp;
    unsigned int rand_gen = prandom_u32();


    for (i = 0; i < 52; i++) {
        j = (((i + 1) * 7) * rand_gen);
        j %= 52;
        
        mutex_lock(&blackjack_mutex);
        tmp = card_numbers[j];
        card_numbers[j] = card_numbers[i];
        card_numbers[i] = tmp;
        mutex_unlock(&blackjack_mutex);
    }
}

static void reset(){
	int i;
	for (i = 0; i < 52; i++){
		mutex_lock(&blackjack_mutex);
		card_numbers[i] = (i + 1);
		mutex_unlock(&blackjack_mutex);
	}
}

static int write_msg(char msg[]){
	mutex_lock(&blackjack_mutex);
	
	if ((strlen(msg_buffer) + 13) > 1024){
		mutex_unlock(&blackjack_mutex);
		return 1;
	}
	
	if (strncmp(msg, "INVALID STATE", 13) == 0){
		strcat(msg_buffer, "Invalid Sequence of States; Perform RESET before SHUFFLE.\n");
	}
	else if (strncmp(msg, "RESET", 5) == 0){
		strcat(msg_buffer, "Deck Reset.\n");
	}
	else if (strncmp(msg, "SHUFFLE", 7) == 0){
		strcat(msg_buffer, "Deck Shuffled.\n");
	}
	
	mutex_unlock(&blackjack_mutex);
	return 0;
}

static int __init blackjack_init(void) {
    int ret = misc_register(&blackjack);
    if (ret < 0){
        printk(KERN_ERR "Blackjack module failed to load\n");
        return ret;
    }
    printk(KERN_ALERT "Blackjack module loaded successfully\n");
    mutex_init(&blackjack_mutex);
    
    current_game.current_state = 0;
    current_game.player_score = 0;
    current_game.dealer_score = 0;
    
    return 0;
} 

static void __exit blackjack_exit(void) {
    misc_deregister(&blackjack);
    printk(KERN_ALERT "Blackjack module unloaded\n");
}

module_init(blackjack_init);
module_exit(blackjack_exit);
