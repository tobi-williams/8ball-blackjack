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
static void shuffle(void); //This function shuffles the array of integers 0 - 51 which correspond to a unique card in a card deck. It uses a psedo random number generator to mix up the cards. It returns void.
static void reset(void); //This function resets the game to its initial state. The game state and all game values are reset and the cards are reset to an ordered state. This function returns void
static void write_msg(char msg[]); //This function writes messages and information to a buffer to be printed in user space based on the actions of the player in the game. It returns void
static int get_card_value(int num); //This function calculates the value of a card based on its number (0 - 51). It checks if the number is valid and then calculates the value. It returns ints. 11 for aces, 10 for face cards and face value for other cards.
static int calculate_score(char player[]); //This function calculates the total score of the player or dealer's hand. It iterates through the hand calculating the value of each card using get_card_value(int). It totals the score and adjusts for aces to be valued as 1 if the total is > 21. It returns int. 
static int deal(void); //This fuction deals a card from the deck. It iterates through the deck to find the first card that hasnt been dealt yet. Once found, it stores the card temporarily and replaces that card value with -1 in the deck for future dealing and then returns the card. It returns int.

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


static const char *card_deck[] = {
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

enum state {
	DISABLED = 0,
	RESET = 1,
	SHUFFLED = 2,
	DEAL = 3,
	END = 4,
	REUSINGDECK = 5,
};

struct game_data {
    enum state current_state;
    int dealer_score;
    int player_score;
    int card_numbers[52];
    int players_hand[15];
    int dealers_hand[15];
};

static char msg_buffer[5120] = {0};
static struct game_data current_game;
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
    size_t bytes_to_copy;
    
    
    if (len >= strlen(msg_buffer)){			//set bytes to copy to not go over the length of the userspace buffer
    	bytes_to_copy = strlen(msg_buffer);
    } else{
    	bytes_to_copy = len;
    }
    
    if(copy_to_user(buff, msg_buffer, bytes_to_copy)){
    	return -EFAULT;
    }
    
    memset(msg_buffer, 0, 5120);
    
    return bytes_to_copy;
}

static ssize_t device_write(struct file *file, const char __user *buff, size_t len, loff_t *offset){
	char command[16];
	int i, card_dealt;

	if (len > (sizeof(command) - 1)){
		return -EINVAL;
	}
	command[len] = '\0';

	if (copy_from_user(command, buff, len)){
		return -EFAULT;
	}

	if (current_game.current_state == 4) {			//check if the game is over
		if (strncasecmp(command, "YES", 3) == 0) {	//if user says yes to continuing with the same deck, reset the scores and their hands, set the game state to "reusingdeck"
			mutex_lock(&blackjack_mutex);
			current_game.current_state = 5;
			current_game.player_score = 0;
			current_game.dealer_score = 0;
			memset(current_game.players_hand, -1, sizeof(current_game.players_hand));
			memset(current_game.dealers_hand, -1, sizeof(current_game.dealers_hand));
			mutex_unlock(&blackjack_mutex);
			
			write_msg("CONTINUE DECK");
		}
		else if (strncasecmp(command, "NO", 2) == 0) {	//if the user wants a new deck, set the gamestate to disabled so they have to begin afresh
			current_game.current_state = 0;
			
			write_msg("NEW DECK");
		}
		else {								//prompt for yes or no if the user enters something different
			write_msg("YES OR NO");
		}
	}



	else if (strncasecmp(command, "RESET", 5) == 0){		//perform reset if the user enters "reset"
		reset();
		write_msg("RESET");
	}
	
	
	
	else if (strncasecmp(command, "SHUFFLE", 7) == 0){		//user enters "shuffle"
		
		if ((current_game.current_state != 1) && (current_game.current_state != 2)){	//print error if user tries to shuffle at the wrong time
			
			write_msg("INVALID STATE");
		} 
		else{
			shuffle();
			write_msg("SHUFFLE");
		}
	}
	
	
	
	else if (strncasecmp(command, "DEAL", 4) == 0){			//user enters "deal"
	
		if ((current_game.current_state != 2) && (current_game.current_state != 5)){	//print error if the user tries to deal at the wrong time
			if (current_game.current_state != 3) {			//invalid deal error if they try to deal without reset and shuffle
				write_msg("INVALID DEAL");
			}
			else {
				write_msg("MULTIPLE DEAL");					//multiple deal error if user tries to deal after already dealing once in the same game
			}
		}
		else {
			calculate_score("PLAYER");	calculate_score("DEALER");
		
			for (i = 0; i < 2; i++){		//deal 2 cards to player
				card_dealt = deal();
				
				if (card_dealt == -1){		//handle the deck running out of cards
					write_msg("EMPTY DECK");
					
					mutex_lock(&blackjack_mutex);
					current_game.current_state = 0;		
					mutex_unlock(&blackjack_mutex);
					break;
				}
				
				mutex_lock(&blackjack_mutex);
				current_game.players_hand[i] = card_dealt;
				mutex_unlock(&blackjack_mutex);
			}
		
			
			for (i = 0; i < 2; i++){		//deal 2 cards to dealer
				card_dealt = deal();
				
				if (card_dealt == -1){		//handle the deck running out of cards
					write_msg("EMPTY DECK");
					
					mutex_lock(&blackjack_mutex);
					current_game.current_state = 0;
					mutex_unlock(&blackjack_mutex);
					break;
				}
				
				mutex_lock(&blackjack_mutex);
				current_game.dealers_hand[i] = card_dealt;
				mutex_unlock(&blackjack_mutex);
			}
			
			mutex_lock(&blackjack_mutex);
			strcat(msg_buffer, "Dealer has dealt 2 initial cards --- Player's hand:\n");	//print out the player's hand
			mutex_unlock(&blackjack_mutex);
			write_msg("PLAYERS HAND");
			
			if (current_game.player_score == 21){	//if player has 21 after the first 2 cards are dealt (a blackjack), player wins, game ends
				write_msg("BLACKJACK");
				
				mutex_lock(&blackjack_mutex);
				current_game.current_state = 4;
				mutex_unlock(&blackjack_mutex);
				
				write_msg("END OF GAME");
			}
			else {									//if the player doesnt have a blackjack after the first 2 card, ask if they want to hit or hold
				write_msg("HIT OR HOLD");
			}
		}
		
	}
	
	
	
	else if (strncasecmp(command, "HIT", 3) == 0){	//user enters "hit"
		
		if (current_game.current_state != 3){		//if user enters hit at the wrong time, print an error
			write_msg("INVALID HIT OR HOLD");
		}
		else {										//else, deal a card
			card_dealt = deal();
				
			if (card_dealt == -1){					//handle the deck running out of cards
				write_msg("EMPTY DECK");
				
				mutex_lock(&blackjack_mutex);
				current_game.current_state = 0;
				mutex_unlock(&blackjack_mutex);
			}
			else {
				i = 0;
				while(current_game.players_hand[i] != -1) {
					i++;
				}
				mutex_lock(&blackjack_mutex);
				current_game.players_hand[i] = card_dealt;
				strcat(msg_buffer, "Player has been dealt an additional card --- Player's hand:\n");
				mutex_unlock(&blackjack_mutex);
				
				write_msg("PLAYERS HAND");
			
				if (current_game.player_score > 21){	//check if player busts after a hit, if they do end the game, dealer wins
					write_msg("PLAYER BUSTS");
					
					mutex_lock(&blackjack_mutex);
					current_game.current_state = 4;
					mutex_unlock(&blackjack_mutex);
					
					write_msg("END OF GAME");
				}
				else {									//if they dont, ask again if they want tohit or hold
					write_msg("HIT OR HOLD");
				}
			}
			
			/*mutex_lock(&blackjack_mutex);
			strcat(msg_buffer, "Player has been dealt an additional card --- Player's hand:\n");
			mutex_unlock(&blackjack_mutex);
			write_msg("PLAYERS HAND");
			
			if (current_game.player_score > 21){
				write_msg("PLAYER BUSTS");
				
				mutex_lock(&blackjack_mutex);
				current_game.current_state = 4;
				mutex_unlock(&blackjack_mutex);
				
				write_msg("END OF GAME");
			}
			else {
				write_msg("HIT OR HOLD");
			}*/
			
		}
		
	}
	
	
	
	else if (strncasecmp(command, "HOLD", 4) == 0){			//user enters "hold"
		
		if (current_game.current_state != 3){				//if they entered hold at a worng time, print an error
			write_msg("INVALID HIT OR HOLD");
		}
		else {												//else let the dealer draw cards
			mutex_lock(&blackjack_mutex);
			strcat(msg_buffer, "Dealer has drawn 2 initial cards --- Dealer's hand:\n");	//print out the cards the dealer initially drew
			mutex_unlock(&blackjack_mutex);
			write_msg("DEALERS HAND");
			
			if (current_game.dealer_score >= 17) {			//if dealer's hand >= 17, check for a winner
				if (current_game.dealer_score >= current_game.player_score) {
					//dealer wins
					write_msg("DEALER WINS");
					
					mutex_lock(&blackjack_mutex);
					current_game.current_state = 5;
					mutex_unlock(&blackjack_mutex);
					
					write_msg("END OF GAME");
				}
				else {
					//player wins
					write_msg("PLAYER WINS");
					
					mutex_lock(&blackjack_mutex);
					current_game.current_state = 4;
					mutex_unlock(&blackjack_mutex);
					
					write_msg("END OF GAME");
				}
			}
			else {											//else, if dealer's hand is < 17, let the dealer draw until it reaches 17
			
				i = 0;
				while(current_game.dealers_hand[i] != -1) {		//find first empty spot on dealers hand to deal a card to
					i++;
				}
			
				while (calculate_score("DEALER") < 17) {
					card_dealt = deal();
						
					if (card_dealt == -1){						//handle the deck running out of cards
						write_msg("EMPTY DECK");
						
						mutex_lock(&blackjack_mutex);
						current_game.current_state = 0;
						mutex_unlock(&blackjack_mutex);
						break;
					}
					else {										//print out the dealer's hand after each card is drawn
						mutex_lock(&blackjack_mutex);
						current_game.dealers_hand[i++] = card_dealt;
						strcat(msg_buffer, "Dealer draws a new card --- Dealer's hand:\n");
						mutex_unlock(&blackjack_mutex);
						write_msg("DEALERS HAND");
					}
				}
				
				
				if (current_game.dealer_score > 21) {			//if dealer draws over 21, dealer busts, player wins, game ends
					//dealer busts
					write_msg("DEALER BUSTS");
					
					mutex_lock(&blackjack_mutex);
					current_game.current_state = 4;
					mutex_unlock(&blackjack_mutex);
					
					write_msg("END OF GAME");
				}
				else {		//if dealer is over 17 but not over 21
					if (current_game.dealer_score >= current_game.player_score) {		//if dealer's hand is closer to 21 than player's hand or equal to player's hand, dealer wins, game ends	
						//dealer wins
						write_msg("DEALER WINS");
						
						mutex_lock(&blackjack_mutex);
						current_game.current_state = 4;
						mutex_unlock(&blackjack_mutex);
						
						write_msg("END OF GAME");
					}
					else {										//if player is closer to 21, player wins, game ends
						//player wins
						write_msg("PLAYER WINS");
						
						mutex_lock(&blackjack_mutex);
						current_game.current_state = 4;
						mutex_unlock(&blackjack_mutex);
						
						write_msg("END OF GAME");
					}
				}	
			}
		}
	}
	
	
	
	else {													//print an invalid command error if an unknown command is entered
		write_msg("INVALID COMMAND.");
	}
	
	
	
	return len;
}

static void shuffle(){
    size_t i, j;
    int tmp;
    unsigned int rand_gen = prandom_u32();

	mutex_lock(&blackjack_mutex);
	current_game.current_state = 2;			//set the state to shuffle
	
    for (i = 0; i < 52; i++) {				// go through each card in the deck and swap it with another randomly selected card in the deck
        j = (((i + 1) * 7) * rand_gen);
        j %= 52;
        tmp = current_game.card_numbers[j];
        current_game.card_numbers[j] = current_game.card_numbers[i];
        current_game.card_numbers[i] = tmp;
    }
    
    mutex_unlock(&blackjack_mutex);
}

static void reset(){			//reset all the game values
	int i;
	
	mutex_lock(&blackjack_mutex);
	current_game.current_state = 1;
	current_game.player_score = 0;
	current_game.dealer_score = 0;
	
	for (i = 0; i < 52; i++){
		current_game.card_numbers[i] = i;
	}

	memset(current_game.players_hand, -1, sizeof(current_game.players_hand));
	memset(current_game.dealers_hand, -1, sizeof(current_game.dealers_hand));
	
	mutex_unlock(&blackjack_mutex);
}

static void write_msg(char msg[]){
	int i;
	char tmp[10];
	
	mutex_lock(&blackjack_mutex);
	
	if ((strlen(msg_buffer) + 75) > 5120){
		mutex_unlock(&blackjack_mutex);
		printk(KERN_ERR "No more space in user message buffer. cat /dev/blackjack to read and clear the buffer\n");
		return;
	}
	
	if (strncmp(msg, "INVALID STATE", 13) == 0){
		strcat(msg_buffer, "Invalid Sequence of Commands; Perform RESET before SHUFFLE.\n");
	}
	else if (strncmp(msg, "INVALID COMMAND", 15) == 0){
		strcat(msg_buffer, "Invalid Command.\n");
	}
	else if (strncmp(msg, "INVALID DEAL", 12) == 0){
		strcat(msg_buffer, "Invalid Sequence of Commands; Perform RESET and SHUFFLE to begin a new game.\n");
	}
	else if (strncmp(msg, "MULTIPLE DEAL", 13) == 0){
		strcat(msg_buffer, "Invalid Sequence of Commands; Cannot DEAL multiple times. Perform HIT or HOLD.\n");
	}
	else if (strncmp(msg, "INVALID HIT OR HOLD", 19) == 0){
		strcat(msg_buffer, "Invalid Sequence of Commands; Perform DEAL before HIT or HOLD.\n");
	}
	else if (strncmp(msg, "RESET", 5) == 0){
		strcat(msg_buffer, "Deck Reset.\n");
	}
	else if (strncmp(msg, "SHUFFLE", 7) == 0){
		strcat(msg_buffer, "Deck Shuffled.\n");
	}
	else if (strncmp(msg, "PLAYERS HAND", 12) == 0){
		for (i = 0; i < 15; i++){					//go through each card in player's hand and print out their suit and values
			if(current_game.players_hand[i] == -1){			
				mutex_unlock(&blackjack_mutex);
				break;
			}
			else{
				strcat(msg_buffer, card_deck[current_game.players_hand[i]]);
			}
		}
		snprintf(tmp, 10, "%d\n\n", calculate_score("PLAYER"));		//calculate total and print it out as well
		strcat(msg_buffer, "Player has a total of ");
		strcat(msg_buffer, tmp);
	}
	else if (strncmp(msg, "DEALERS HAND", 12) == 0){
		for (i = 0; i < 15; i++){					//go through each card in player's hand and print out their suit and values
			if(current_game.dealers_hand[i] == -1){
				mutex_unlock(&blackjack_mutex);
				break;
			}
			else{
				strcat(msg_buffer, card_deck[current_game.dealers_hand[i]]);
			}
		}
		snprintf(tmp, 10, "%d\n\n", calculate_score("DEALER"));		//calculate total and print it out
		strcat(msg_buffer, "Dealer has a total of ");
		strcat(msg_buffer, tmp);
	}
	else if (strncmp(msg, "EMPTY DECK", 10) == 0){
		strcat(msg_buffer, "Deck is empty. RESET and SHUFFLE to continue playing.\n");
	}
	else if (strncmp(msg, "CONTINUE DECK", 13) == 0){
		strcat(msg_buffer, "You are continuing with the same deck. Enter DEAL to play\n");
	}
	else if (strncmp(msg, "NEW DECK", 8) == 0){
		strcat(msg_buffer, "You are using a new deck. RESET and SHUFFLE to continue playing.\n");
	}
	else if (strncmp(msg, "YES OR NO", 9) == 0){
		strcat(msg_buffer, "Invalid Input. Enter YES or NO.\n");
	}
	else if (strncmp(msg, "BLACKJACK", 9) == 0){
		strcat(msg_buffer, "Blackjack! Player wins.\n");
	}
	else if (strncmp(msg, "PLAYER BUSTS", 12) == 0){
		strcat(msg_buffer, "Player Busts! Dealer Wins.\n");
	}
	else if (strncmp(msg, "DEALER BUSTS", 12) == 0){
		strcat(msg_buffer, "Dealer Busts! Player Wins.\n");
	}
	else if (strncmp(msg, "PLAYER WINS", 11) == 0){
		strcat(msg_buffer, "Player is closer to 21. Player Wins!\n");
	}
	else if (strncmp(msg, "DEALER WINS", 11) == 0){
		strcat(msg_buffer, "Dealer Wins!\n");
	}
	else if (strncmp(msg, "HIT OR HOLD", 9) == 0){
		strcat(msg_buffer, "HIT or HOLD?\n");
	}
	else if (strncmp(msg, "END OF GAME", 11) == 0){
		strcat(msg_buffer, "Game is over. Do you want to play again using the same deck? (YES or NO).\n");
	}
	else {
		printk(KERN_ALERT "No such message exists.");
	}
	
	mutex_unlock(&blackjack_mutex);
	return;
}

static int get_card_value(int num){		//calculate the value of a card based on its number 0 - 51 using the modulus operator
	int value;
	
	if ((num < 0) || (num > 51)){
		return -1;
	}
	
	value = num % 13;
	value += 1;
	
	if (value == 1){
		return 11;
	}
	else if (value > 10){
		return 10;
	}
	else{
		return value;
	}
}

static int calculate_score(char player[]){
	int i, total, ret, aces;
	total = 0;	aces = 0;
	
	if (strncmp(player, "PLAYER", 6) == 0){
		for (i = 0; i < 15; i++){					//loop throught the player's hand
			if(current_game.players_hand[i] == -1){		
				
				while(total > 21){			//drop the values of any aces from 11 to 1 if the total goes over 21
					if (aces > 0){
						total -= 10;
						aces--;
					}
					else{
						break;
					}
				}
				
				mutex_lock(&blackjack_mutex);
				current_game.player_score = total;
				mutex_unlock(&blackjack_mutex);
				return total;
			}
			
			ret = get_card_value(current_game.players_hand[i]);
			if(ret == -1){
				return -1;
			}
			else if (ret == 11){			//Ace condition
				aces++;
				total += ret;
			}
			else {
				total += ret;
			}
		}
	}
	else if ((strncmp(player, "DEALER", 6) == 0)){
		for (i = 0; i < 15; i++){					//loop throught the dealer's hand
			if(current_game.dealers_hand[i] == -1){
			
				while(total > 21){						//drop the values of any aces from 11 to 1 if the total goes over 21
					if (aces > 0){
						total -= 10;
						aces--;
					}
					else{
						break;
					}
				}
			
				mutex_lock(&blackjack_mutex);
				current_game.dealer_score = total;
				mutex_unlock(&blackjack_mutex);
				return total;
			}
			
			ret = get_card_value(current_game.dealers_hand[i]);
			if(ret == -1){
				return -1;
			}
			else if (ret == 11){			//Ace condition
				aces++;
				total += ret;
			}
			else {
				total += ret;
			}
		}
	}
	else {
		return -1;
	}
	
	return total;
}

static int deal(){
	int i, tmp;
	
	//return the next card in the deck, replace each returned card with -1
	for (i = 0; i < 52; i++){
		if (current_game.card_numbers[i] == -1){
			continue;
		}
		else{
			tmp = current_game.card_numbers[i];
			mutex_lock(&blackjack_mutex);
			current_game.card_numbers[i] = -1;
			current_game.current_state = 3;
			mutex_unlock(&blackjack_mutex);
			return tmp;
		}
	}
	
	return -1;
}

static int __init blackjack_init(void) {
    int ret = misc_register(&blackjack);
    if (ret < 0){
        printk(KERN_ERR "Blackjack module failed to load\n");
        return ret;
    }
    printk(KERN_ALERT "Blackjack module loaded successfully\n");
    mutex_init(&blackjack_mutex);
    
    //initialize game values
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
