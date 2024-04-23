/**
 * comp2017 - assignment 3
 * Renjie He
 * rehe3414
 */

#include "pe_exchange.h"

volatile sig_atomic_t num_alive_traders = 0;;
struct pid_circular_queue pid_queue;
struct trader_list traders;
struct product_list products;
struct order_list *order_book;
long int exchange_fees;

#ifndef TESTING
int main(int argc, char** argv){
    if(argc < 3) {
        printf("Not enough arguments. \nUsage: %s products.txt <./trader_a> ... <./trader_n>\n", argv[0]);
        return 1;
    }

    // Read products info from the product file.
    read_product_file(argv[1], &products);

    // Register traders
    traders = init_traders(argc-2, argv+2, &products);

    // Initialize circular pid queue
    init_pid_queue(&pid_queue, traders.num_traders * QUEUE_SIZE_BASE);

    // Register sigusr1 handler for exchange trader notification
    struct sigaction sa_usr1 = {0};
    sa_usr1.sa_flags = SA_SIGINFO;
    sa_usr1.sa_sigaction = exchange_handler;
    sigemptyset(&sa_usr1.sa_mask);
    if(sigaction(SIGUSR1, &sa_usr1, NULL) == -1) {
        perror("Error registring sa for SIGRS1");
        exit(1);
    }

    // Register sigchild handler to get child pids
    struct sigaction sa_child = {0};
    sa_child.sa_sigaction = trader_disconnect_handler;
    sigemptyset(&sa_child.sa_mask);
    sa_child.sa_flags = SA_SIGINFO;
    if(sigaction(SIGCHLD, &sa_child, NULL) == -1) {
        perror("Error registring sa for SIGCHILD");
        exit(1);
    }

    // Initialize order book, contains order lists for each product
    order_book = init_order_book(products.num_products);

    // Print exchange starting info
    show_pex_start(&products);

    // Make fifos, start traders and connect
    int *fds_exchange = NULL; //fds_exchange array for writing
    int *fds_trader = NULL; //fds_trader array for reading
    make_connect_fifos(&fds_exchange, &fds_trader, &traders);

    // Send market open message to traders
    market_open_msg(fds_exchange, &traders);

    // Event loop
    while(1) {
        // All traders disconnected
        if(num_alive_traders == 0) {
            break;
        }

        if(is_empty_queue(&pid_queue)){
            // Wait for trader signal
            pause();
        } else {
            // Communicate with trader i, the front in the queue
            int trader_pid = pid_dequeue(&pid_queue);
            int i = get_traderid_by_pid(&traders, trader_pid);
            // No such child trader -1
            if(i != -1 && traders.trader_arr[i].is_alive) {

                // Set signal mask
                sigset_t mask, oldmask;
                sigemptyset(&mask);
                sigaddset(&mask, SIGCHLD);
                // Block SIGCHLD to avoid interruption

                // Read message from trader ----
                struct order received_order;
                enum OrderResponseType response;
                response = parse_command(fds_trader, i, &received_order);

                sigprocmask(SIG_BLOCK, &mask, &oldmask);

                // Send response to the trader ----
                send_order_response(response, fds_exchange, &received_order);

                // Notify other traders ----
                notify_traders(response, fds_exchange, &received_order);

                // Process order ----
                process_order(response, fds_exchange, &received_order);

                // Unblocking SIGCHLD
                sigprocmask(SIG_SETMASK, &oldmask, NULL);
            }
        }
    }

    // Trading completed, print the ending information
    show_trading_end(exchange_fees);

    // Teardown
    for(int i=0; i<traders.num_traders; i++) {
        char fifo_exchange[BUF_LEN] = {'\0'};
        char fifo_trader[BUF_LEN] = {'\0'};
        snprintf(fifo_exchange, BUF_LEN, FIFO_EXCHANGE, i);
        snprintf(fifo_trader, BUF_LEN, FIFO_TRADER, i);

        close(fds_exchange[i]);
        close(fds_trader[i]);
        unlink(fifo_exchange);
        unlink(fifo_trader);
    }

    // Free traders as well as positions
    free_traders(&traders);

    // Free the pid queue
    free_pid_queue(&pid_queue);
    
    // Free product list
    free_product_list(&products);
    // Free order book
	free_order_book(order_book, products.num_products);
    // Free fds
    free_fds(fds_exchange, fds_trader);

    return 0;
}
#endif

void init_pid_queue(struct pid_circular_queue *pid_queue, int size) {
    pid_queue->pid_arr = (int*)malloc(size * sizeof(int));
    memset(pid_queue->pid_arr, 0, size * sizeof(int));
    pid_queue->front = -1;
    pid_queue->rear = -1;
    pid_queue->max_size = size;
}

int is_empty_queue(struct pid_circular_queue *pid_queue) {
    return pid_queue->front == -1;
}

void pid_enqueue(struct pid_circular_queue *pid_queue, int pid) {
    if(is_empty_queue(pid_queue)) {
        pid_queue->front = 0;
        pid_queue->rear = 0;
    } else {
        pid_queue->rear = (pid_queue->rear + 1) % pid_queue->max_size;
        // If the rear goes back to front after enqueue
        if(pid_queue->rear == pid_queue->front) {
            // Have overwritten the initial front element and update front
            pid_queue->front = (pid_queue->front + 1) % pid_queue->max_size;
        }
    }
    // Add the pid to the rear
    pid_queue->pid_arr[pid_queue->rear] = pid;
}

int pid_dequeue(struct pid_circular_queue *pid_queue) {
    if(is_empty_queue(pid_queue)) {
        perror("Empty pid queue");
        exit(1);
    }
    int pid = pid_queue->pid_arr[pid_queue->front];
    // If the queue becomes empty
    if(pid_queue->front == pid_queue->rear) {
        pid_queue->front = -1;
        pid_queue->rear = -1;
    } else {
        // Update front
        pid_queue->front = (pid_queue->front + 1) % pid_queue->max_size;
    }

    return pid;
}

void free_pid_queue(struct pid_circular_queue *pid_queue) {
    free(pid_queue->pid_arr);
    pid_queue->pid_arr = NULL;
    pid_queue->front = -1;
    pid_queue->rear = -1;
    pid_queue->max_size = 0;
}

void exchange_handler(int sig, siginfo_t* info, void* ucontext) {
    pid_enqueue(&pid_queue, info->si_pid);
}

void trader_disconnect_handler(int sig, siginfo_t* info, void* ucontext) {
    int status;
    waitpid(info->si_pid, &status, 0);
    // Check the disconnected trader
    // if(WIFEXITED(status) && WEXITSTATUS(status)==0) {
    int id = get_traderid_by_pid(&traders, info->si_pid);
    if(id != -1) {
        traders.trader_arr[id].is_alive = 0;
        num_alive_traders--;
        printf(LOG_PREFIX" Trader %d disconnected\n", id);
    }
}

void read_product_file (const char *filename, struct product_list* products) {
    FILE *fp_product = fopen(filename, "r");
    if(fp_product == NULL) {
        perror("Error opening product file");
        exit(1);
    }

    // Get number of products
    int product_num_ret = fscanf(fp_product,"%d\n", &products->num_products);
    if(product_num_ret != 1) {
        perror("Error reading num of products");
        exit(1);
    }

    products->names = (char(*)[PRODUCT_NAME_MAX])malloc(products->num_products * sizeof(char[PRODUCT_NAME_MAX]));

    // Get each product
    for(int i = 0; i < products->num_products; i++) {
        char product[PRODUCT_NAME_MAX] = {'\0'};
        if(fscanf(fp_product, "%"TO_STRING(PRODUCT_STR_LEN)"s\n", product) != 1) {
                perror("Error reading product name");
                exit(1);
        }
        // Check alphanumeric
        for(int j = 0; product[j] != '\0'; j++) {
            if(!isalnum((unsigned char)product[j])) {
                perror("Error reading product name alphanumeric");
                exit(1);
            }
        }
        strcpy(products->names[i], product);
    }

    fclose(fp_product);
}

struct trader_list init_traders (int num_traders, char **trader_names, struct product_list *products) {
    struct trader_list traders;
    traders.num_traders = num_traders;
    traders.trader_arr = (struct trader*)malloc(num_traders * sizeof(struct trader));

    // Initialize each trader
    for(int i=0; i<traders.num_traders; i++) {
        traders.trader_arr[i].id = i;
        traders.trader_arr[i].name = trader_names[i];
        traders.trader_arr[i].num_orders = 0;
        traders.trader_arr[i].pid = -1; // Initialize as -1 until fork process
        traders.trader_arr[i].is_alive = 0; // Initialize as not alive until fork process
        traders.trader_arr[i].positions = (struct position*)malloc(products->num_products * sizeof(struct position));

        // Initialize the product positions of each trader
        for(int j=0; j<products->num_products; j++) {
            strcpy(traders.trader_arr[i].positions[j].product, products->names[j]);
            traders.trader_arr[i].positions[j].qty = 0;
            traders.trader_arr[i].positions[j].profit = 0;
        }

    }
    return traders;
}

void free_traders(struct trader_list *traders) {
    // Free positions of each trader
    for(int i = 0; i < traders->num_traders; i++) {
        free(traders->trader_arr[i].positions);
    }
    // Free traders array
    free(traders->trader_arr);
}

int get_traderid_by_pid(struct trader_list *traders, int pid) {
    for(int id=0; id<traders->num_traders; id++) {
        if(traders->trader_arr[id].pid == pid) {
            return id;
        }
    }
    return -1; // No such trader
}

struct order_list* init_order_book(int num_products) {
   struct order_list *order_book = (struct order_list*)malloc(num_products * sizeof(struct order_list));

   // Initialize the buy and sell order list for each product
   for(int i=0; i<num_products; i++) {
        order_book[i].buy_head = NULL;
        order_book[i].sell_head = NULL;
        order_book[i].buy_list_size = 0;
        order_book[i].sell_list_size = 0;
        order_book[i].buy_levels = 0;
        order_book[i].sell_levels = 0;
   }
   return order_book;
}

void free_order_list(struct order *head) {
    struct order* cursor;

    while (head != NULL)
    {
       cursor = head;
       head = head->next;
       free(cursor);
    }
}

void free_order_book(struct order_list *order_book, int num_products) {
    for (int i = 0; i < num_products; i++) {
        free_order_list(order_book[i].buy_head);
        free_order_list(order_book[i].sell_head);
    }

    free(order_book);
}

void show_pex_start(struct product_list *products) {
    printf(LOG_PREFIX" Starting\n");
    printf(LOG_PREFIX" Trading %d products:", products->num_products);
    for(int i=0; i<products->num_products; i++) {
        printf(" %s", products->names[i]);
    }
    printf("\n");
}

void make_connect_fifos(int **fds_exchange, int **fds_trader, struct trader_list *traders) {
    // Allocate memory for fds
	*fds_exchange = (int*)malloc(traders->num_traders * sizeof(int)); //fds_exchange array
    *fds_trader = (int*)malloc(traders->num_traders * sizeof(int)); //fds_trader array

    // Make fifos - launch traders - connect to named pipes
    for(int id=0; id<traders->num_traders; id++) {
        char fifo_exchange[BUF_LEN] = {'\0'};
        char fifo_trader[BUF_LEN] = {'\0'};
        snprintf(fifo_exchange, BUF_LEN, FIFO_EXCHANGE, id);
        snprintf(fifo_trader, BUF_LEN, FIFO_TRADER, id);

        // Make fifos
        unlink(fifo_exchange);
        int mk_exchange = mkfifo(fifo_exchange, 0666);
        if(mk_exchange == -1) {
            perror("Error making exchange fifo");
            exit(1);
        }
        printf(LOG_PREFIX" Created FIFO %s\n", fifo_exchange);

		unlink(fifo_trader);
        int mk_trader = mkfifo(fifo_trader, 0666);
        if(mk_trader == -1) {
            perror("Error making trader fifo");
            exit(1);
        }
        printf(LOG_PREFIX" Created FIFO %s\n", fifo_trader);

        // Launch trader
        launch_trader(traders, id);

        // Connect to named pipes
        (*fds_exchange)[id] = open(fifo_exchange, O_WRONLY);
        if ((*fds_exchange)[id] == -1) {
            perror("Error opening fifo_exchange");
            exit(1);
        }
        printf(LOG_PREFIX" Connected to %s\n", fifo_exchange);

        (*fds_trader)[id] = open(fifo_trader, O_RDONLY);
        if ((*fds_trader)[id] == -1) {
            perror("Error opening fifo_trader");
            exit(1);
        }
        printf(LOG_PREFIX" Connected to %s\n", fifo_trader);
    }
}

void launch_trader(struct trader_list *traders, int trader_id) {
    int pid = fork();
    if (pid > 0) {
        // Exchange process, record each trader pid
        traders->trader_arr[trader_id].is_alive = 1;
        num_alive_traders++;
        traders->trader_arr[trader_id].pid = pid;
        printf(LOG_PREFIX " Starting trader %d (%s)\n", trader_id, traders->trader_arr[trader_id].name);
    } else if (pid == 0) {
        // Child process, execute current trader
        char trader_id_str[INT_LEN] = {'\0'};
        snprintf(trader_id_str, INT_LEN, "%d", trader_id);  // Get the trader id string
        execl(traders->trader_arr[trader_id].name, traders->trader_arr[trader_id].name, trader_id_str, NULL);  // Execute ./trader_x n

        // If execl failed
        perror("Error executing trader");
        exit(1);
    } else {
        perror("Error forking");
        exit(1);
    }
}

void market_open_msg(int* fds_exchange, struct trader_list* traders) {
    for(int id=0; id<traders->num_traders; id++) {
        // Send market open message
        char* market_open = "MARKET OPEN;";
        write(fds_exchange[id], market_open, strlen(market_open));
        kill(traders->trader_arr[id].pid, SIGUSR1);
    }
}


int get_productid_by_name(char *product_name, struct product_list *products) {
	for(int i=0; i<products->num_products; i++) {
		if(strcmp(products->names[i], product_name) == 0) {
			return i;
		}
	}
	return -1; // No such product
}

enum OrderResponseType parse_command(int *fds_trader, int trader_id, struct order *received_order) {
    // Read message from trader
    char command[BUF_LEN] = {'\0'};
    ssize_t read_len = read(fds_trader[trader_id], command, BUF_LEN-1);
    // If read nothing
    if(read_len <= 0) {
        return NORESPONSE;
    }

    if(read_len > 0 && command[read_len-1] == ';') {
        command[read_len-1] = '\0';
        printf(LOG_PREFIX" [T%d] Parsing command: <%s>\n", trader_id, command);

        received_order->trader_id = trader_id;
        // Check the command type
        if(strstr(command, "BUY") && is_valid_buy(command, trader_id, received_order)) {
            return ACCEPTED;
        } else if(strstr(command, "SELL") && is_valid_sell(command, trader_id, received_order)){
            return ACCEPTED;
        } else if(strstr(command, "AMEND") && is_valid_amend(command, trader_id, received_order)) {
            return AMENDED;
        } else if(strstr(command, "CANCEL") && is_valid_cancel(command, trader_id, received_order)) {
            return CANCELLED;
        } else {
            received_order->order_type = INVALID_ORDER;
            return INVALID;
        }
    }

    // Invalid reading or no semicolon at end
    return INVALID;
}

int is_valid_buy(char *command, int trader_id, struct order *received_order) {
    int order_id;
    char product[PRODUCT_NAME_MAX];
    int qty;
    int price;

    int s_ret = sscanf(command, "BUY %d %s %d %d", &order_id, product, &qty, &price);
    // Error handling
    // Check invalid format, 4 arguments
    if(s_ret != BUY_CMD_ARGS) {
        return 0;
    }

    // Check whether reading result is the same as initial command
    char read_buf[BUF_LEN] = {'\0'};
    snprintf(read_buf, sizeof(read_buf),"BUY %d %s %d %d", order_id, product, qty, price);
    if(strcmp(read_buf, command) != 0) {
        return 0;
    }

    // Check order id
    if(order_id < 0 || order_id > MAX_VALUE) {
        return 0;
    }
    if(traders.trader_arr[trader_id].num_orders != order_id) {
        // Not match the trader's order id
        return 0;
    }

    // Check product
    int product_idx = get_productid_by_name(product, &products);
    if(product_idx == -1) {
        // No such product
        return 0;
    }

    // Check quantity and price
    if(qty < MIN_VALUE || qty > MAX_VALUE) {
        return 0;
    }
    if(price < MIN_VALUE || price > MAX_VALUE) {
        return 0;
    }
    
    // Valid buy order
    received_order->order_id = order_id;
    strncpy(received_order->product, product, sizeof(received_order->product) - 1);
    received_order->order_type = BUY;
    received_order->qty = qty;
    received_order->price = price;
    received_order->trader_id = trader_id;
    received_order->next = NULL;
    // traders.trader_arr[trader_id].num_orders ++;
    return 1;
}

int is_valid_sell(char *command, int trader_id, struct order *received_order) {
    int order_id;
    char product[PRODUCT_NAME_MAX];
    int qty;
    int price;

    int s_ret = sscanf(command, "SELL %d %s %d %d", &order_id, product, &qty, &price);
    // Error handling
    // Check invalid format
    if(s_ret != SELL_CMD_ARGS) {
        return 0;
    }

    // Check whether reading result is the same as initial command
    char read_buf[BUF_LEN] = {'\0'};
    snprintf(read_buf, sizeof(read_buf),"SELL %d %s %d %d", order_id, product, qty, price);
    if(strcmp(read_buf, command) != 0) {
        return 0;
    }

    // Check order id
    if(order_id < 0 || order_id > MAX_VALUE) {
        return 0;
    }
    if(traders.trader_arr[trader_id].num_orders != order_id) {
        // Not match the trader's order id
        return 0;
    }

    // Check product
    int product_idx = get_productid_by_name(product, &products);
    if(product_idx == -1) {
        // No such product
        return 0;
    }

    // Check quantity and price
    if(qty < MIN_VALUE || qty > MAX_VALUE) {
        return 0;
    }
    if(price < MIN_VALUE || price > MAX_VALUE) {
        return 0;
    }

    // Valid sell order
    received_order->order_id = order_id;
    strncpy(received_order->product, product, sizeof(received_order->product) - 1);
    received_order->order_type = SELL;
    received_order->qty = qty;
    received_order->price = price;
    received_order->trader_id = trader_id;
    received_order->next = NULL;
    // traders.trader_arr[trader_id].num_orders ++;
    return 1;
}

int is_valid_amend(char *command, int trader_id, struct order *received_order) {
    int order_id;
    int qty;
    int price;

    int s_ret = sscanf(command, "AMEND %d %d %d", &order_id, &qty, &price);
    // Error handling
    // Check invalid format
    if(s_ret != AMEND_CMD_ARGS) {
        return 0;
    }

    // Check whether reading result is the same as initial command
    char read_buf[BUF_LEN] = {'\0'};
    snprintf(read_buf, sizeof(read_buf),"AMEND %d %d %d", order_id, qty, price);
    if(strcmp(read_buf, command) != 0) {
        return 0;
    }

    // Check order id
    if(order_id < 0 || order_id > MAX_VALUE) {
        return 0;
    }
    if(traders.trader_arr[trader_id].num_orders <= order_id) {
        // Trader has no such order id
        return 0;
    }

    // Check quantity and price
    if(qty < MIN_VALUE || qty > MAX_VALUE) {
        return 0;
    }
    if(price < MIN_VALUE || price > MAX_VALUE) {
        return 0;
    }

    // Valid amend order
    for(int i=0; i<products.num_products; i++) {
        struct order_list *product_orders = &(order_book[i]);
        struct order *buy_cursor = product_orders->buy_head;
        struct order *sell_cursor = product_orders->sell_head;

        // Check amend buy order
        while(buy_cursor) {
            if(buy_cursor->order_id == order_id && buy_cursor->trader_id == trader_id) {
                received_order->order_id = order_id;
                received_order->trader_id = trader_id;
                received_order->order_type = BUY;
                received_order->price = price;
                strncpy(received_order->product, buy_cursor->product, sizeof(received_order->product) - 1);
                received_order->next = buy_cursor; // Link the new order to the old one
                received_order->qty = qty;

                return 1;
            } else{
                buy_cursor = buy_cursor->next;
            }
        }

        // Check amend sell order
        while(sell_cursor) {
            if(sell_cursor->order_id == order_id && sell_cursor->trader_id == trader_id) {
                received_order->order_id = order_id;
                received_order->trader_id = trader_id;
                received_order->order_type = SELL;
                received_order->price = price;
                strncpy(received_order->product, sell_cursor->product, sizeof(received_order->product) - 1);
                received_order->next = sell_cursor; // Link the new order to the old one
                received_order->qty = qty;

                return 1;
            } else {
                sell_cursor = sell_cursor->next;
            }
        }

    }

    // No existing order to amend
    return 0;
}

int is_valid_cancel(char *command, int trader_id, struct order *received_order) {
    int order_id;

    int s_ret = sscanf(command, "CANCEL %d", &order_id);
    // Error handling
    // Check invalid format
    if(s_ret != 1) {
        return 0;
    }

    // Check whether reading result is the same as initial command
    char read_buf[BUF_LEN] = {'\0'};
    snprintf(read_buf, sizeof(read_buf),"CANCEL %d", order_id);
    if(strcmp(read_buf, command) != 0) {
        return 0;
    }

    // Check order id
    if(order_id < 0 || order_id > MAX_VALUE) {
        return 0;
    }
    if(traders.trader_arr[trader_id].num_orders <= order_id) {
        // Trader has no such order id
        return 0;
    }

    // Exxisting order
    for(int i=0; i<products.num_products; i++) {
        struct order_list *product_orders = &(order_book[i]);
        struct order *buy_cursor = product_orders->buy_head;
        struct order *sell_cursor = product_orders->sell_head;

        // Find in buy orders
        while(buy_cursor) {
            if(buy_cursor->order_id == order_id && buy_cursor->trader_id == trader_id) {
                received_order->trader_id = trader_id;
                received_order->next = buy_cursor; // Points to the canceled order
                received_order->order_type = BUY;
                received_order->order_id = order_id;
                received_order->price = 0;
                received_order->qty = 0;
                strncpy(received_order->product, buy_cursor->product, sizeof(received_order->product) - 1);

                return 1;
            } else{
                buy_cursor = buy_cursor->next;
            }
        }

        // Check amend buy order
        while(sell_cursor) {
            if(sell_cursor->order_id == order_id && sell_cursor->trader_id == trader_id) {
                received_order->trader_id = trader_id;
                received_order->next = sell_cursor; // Points to the canceled order
                received_order->order_type = SELL;
                received_order->order_id = order_id;
                received_order->price = 0;
                received_order->qty = 0;
                strncpy(received_order->product, sell_cursor->product, sizeof(received_order->product) - 1);

                return 1;
            } else {
                sell_cursor = sell_cursor->next;
            }
        }

    }

    // No existing order to cancel
    return 0;

}

void send_order_response(enum OrderResponseType response, int *fds_exchange, struct order *received_order) {
    char write_buf[BUF_LEN] = {'\0'};
    int order_id = received_order->order_id;
    int trader_id = received_order->trader_id;

    switch (response) {
        case ACCEPTED:
            snprintf(write_buf, BUF_LEN,"ACCEPTED %d;", order_id);
            traders.trader_arr[trader_id].num_orders++;
            break;
            
        case AMENDED:
            snprintf(write_buf, BUF_LEN,"AMENDED %d;", order_id);
            break;
        
        case CANCELLED:
            snprintf(write_buf, BUF_LEN,"CANCELLED %d;", order_id);
            break;

        case INVALID:
            strncpy(write_buf, "INVALID;", BUF_LEN);
            break;

        default:
            return;
    }

    // Write response to the trader
    write(fds_exchange[trader_id], write_buf, strlen(write_buf));
    kill(traders.trader_arr[trader_id].pid, SIGUSR1);
}

void notify_traders(enum OrderResponseType response, int *fds_exchange, struct order *received_order) {
    char write_buf[BUF_LEN] = {'\0'};
    int trader_id = received_order->trader_id;
    char *product = received_order->product;
    int qty = received_order->qty;
    int price = received_order->price;

    switch (response) {
        case ACCEPTED:
            if(received_order->order_type == BUY) {
                snprintf(write_buf, BUF_LEN,"MARKET BUY %s %d %d;",product, qty, price);
            } else if(received_order->order_type == SELL) {
                snprintf(write_buf, BUF_LEN, "MARKET SELL %s %d %d;",product, qty, price);
            }
            break;
            
        case AMENDED:
            if(received_order->order_type == BUY) {
                snprintf(write_buf, BUF_LEN, "MARKET BUY %s %d %d;",product, qty, price);
            } else if(received_order->order_type == SELL) {
                snprintf(write_buf, BUF_LEN, "MARKET SELL %s %d %d;",product, qty, price);
            }
            break;
        
        case CANCELLED:
            if(received_order->order_type == BUY) {
                snprintf(write_buf, BUF_LEN, "MARKET BUY %s 0 0;",product);
            } else if(received_order->order_type == SELL) {
                snprintf(write_buf, BUF_LEN, "MARKET SELL %s 0 0;",product);
            }
            break;

        default:
            return;
    }

    // Notify each trader in the exchange except the oder owner
    for(int id=0; id<traders.num_traders; id++) {
        if(traders.trader_arr[id].is_alive && id != trader_id) {
            write(fds_exchange[id], write_buf, strlen(write_buf));
            kill(traders.trader_arr[id].pid, SIGUSR1);
        }
    }
}

// Process the order read from trader message
void process_order(enum OrderResponseType response, int *fds_exchange, struct order *received_order) {

    switch (response) {
        case ACCEPTED:
            if(received_order->order_type == BUY) {
                handle_buy(received_order, fds_exchange);
            } else if(received_order->order_type == SELL) {
                handle_sell(received_order, fds_exchange);
            }
            break;
 
        case AMENDED:
            handle_amend(received_order, fds_exchange);
            break;
        
        case CANCELLED:
            handle_cancel(received_order, fds_exchange);
            break;

        default:
            return;
    }

    show_order_book(order_book);
    show_positions(&traders);
}


void handle_buy(struct order* received_order, int *fds_exchange) {
    int product_idx = get_productid_by_name(received_order->product, &products);

    // Get the order list for current product
    struct order_list* product_orders = &(order_book[product_idx]);

    // Check the sell list for match orders
    struct order* sell_cursor = product_orders->sell_head;
    struct order* sell_prev = NULL;

    // Iterate the sell list
    while (sell_cursor) {
		// Check enough quantity and price
        if (received_order->price >= sell_cursor->price &&
            received_order->qty > 0) {
            int match_qty;
            // The match qty should be the smaller one of two matching orders
            if (sell_cursor->qty < received_order->qty) {
                match_qty = sell_cursor->qty;
            } else {
                match_qty = received_order->qty;
            }

            long int match_value = (long int)match_qty * sell_cursor->price;
            long int match_fee = (long int)round(match_value * FEE_PERCENTAGE / 100.0);

            // Print the matching infomation
			printf(LOG_PREFIX" Match: Order %d [T%d], New Order %d [T%d], value: $%ld, fee: $%ld.\n", sell_cursor->order_id, sell_cursor->trader_id, received_order->order_id, received_order->trader_id, match_value, match_fee);

            // Notify corresponding traders
            notify_filler(fds_exchange, sell_cursor->trader_id, sell_cursor->order_id, match_qty);
            notify_filler(fds_exchange, received_order->trader_id, received_order->order_id, match_qty);

			// Update qty after matching
			received_order->qty -= match_qty;
			sell_cursor->qty -= match_qty;

            // Update positions for buyer
			traders.trader_arr[received_order->trader_id].positions[product_idx].qty += match_qty;
			traders.trader_arr[received_order->trader_id].positions[product_idx].profit -= (match_value + match_fee);

			// Update positions for seller
            traders.trader_arr[sell_cursor->trader_id].positions[product_idx].qty -= match_qty;
            traders.trader_arr[sell_cursor->trader_id].positions[product_idx].profit += match_value;

			// Update sell list
			if(sell_cursor->qty == 0) {
                int removed_sell_level = 0; // The level to be removed from the sell list
                // Check whether removing the order would reduce sell levels
                if(sell_cursor->next == NULL || sell_cursor->price != sell_cursor->next->price) {
                    removed_sell_level = 1;
                }

                // Remove the matching sell order
				if(sell_prev) {
					sell_prev->next = sell_cursor->next;
				} else {
					product_orders->sell_head = sell_cursor->next;
				}

				struct order *temp = sell_cursor;
				sell_cursor = sell_cursor->next;
				free(temp);

                // Update the sell list size and levels
				product_orders->sell_list_size --;
                product_orders->sell_levels -=  removed_sell_level;
			}

            // Update exchange fees
            exchange_fees += match_fee;
        } else {
            // No matching order
            // Since the sell list is arranged by price from lowest to highest
            break;
		}
    }


	// Check remaining quantity in the received buy order
	if(received_order->qty > 0) {
		// Add new order node to buy list
        struct order* new_node = (struct order*)malloc(sizeof(struct order));
        memcpy(new_node, received_order, sizeof(struct order));
        new_node->next = NULL;

        int added_buy_level = 1; // The level to be added to the buy list

        // If empty list, add new node as head
        if (product_orders->buy_list_size == 0) {
            product_orders->buy_head = new_node;
        } else {
            // Add new node to buy list in sorted order (hign to low)
            struct order* buy_cursor = product_orders->buy_head;  // Set cursor
         	struct order* buy_prev = NULL;  // Set previous node of cursor
            while (buy_cursor) {
				// Check the price
                if (buy_cursor->price >= new_node->price) {
                    // If the buy level exists, no added level
                    if(buy_cursor->price == new_node->price) {
                        added_buy_level = 0;
                    }   
					buy_prev = buy_cursor;
					buy_cursor = buy_cursor->next;
				} else {
                    break;
                }
			}

            // Insert the new node
            if (buy_prev) {
				new_node->next = buy_prev->next;
				buy_prev->next = new_node;
			} else {
				// Insert before the head node as new head
				new_node->next = product_orders->buy_head;
				product_orders->buy_head = new_node;
			}
        }

        // Update the buy list size and levels
        product_orders->buy_list_size++;
        product_orders->buy_levels += added_buy_level;
    }

}


void handle_sell(struct order  *received_order, int *fds_exchange) {
    int product_idx = get_productid_by_name(received_order->product, &products);

    // Get the order list for current product
    struct order_list* product_orders = &(order_book[product_idx]);

    // Check the buy list for matching orders
    struct order* buy_cursor = product_orders->buy_head;
    struct order* buy_prev = NULL;

    // Iterate the buy list
    while (buy_cursor) {
		// Check enough quantity and price
        if (received_order->price <= buy_cursor->price &&
            received_order->qty > 0) {
            int match_qty;
            // The match qty should be the smaller one of two matching orders
            if (buy_cursor->qty < received_order->qty) {
                match_qty = buy_cursor->qty;
            } else {
                match_qty = received_order->qty;
            }

            long int match_value = (long int)match_qty * buy_cursor->price;
            long int match_fee = (long int)round(match_value * FEE_PERCENTAGE / 100.0);

            // Print the matching infomation
			printf(LOG_PREFIX" Match: Order %d [T%d], New Order %d [T%d], value: $%ld, fee: $%ld.\n", buy_cursor->order_id, buy_cursor->trader_id, received_order->order_id, received_order->trader_id, match_value, match_fee);

            // Notify corresponding traders
            notify_filler(fds_exchange, buy_cursor->trader_id, buy_cursor->order_id, match_qty);
            notify_filler(fds_exchange, received_order->trader_id, received_order->order_id, match_qty);

			// Update qty after matching
			received_order->qty -= match_qty;
			buy_cursor->qty -= match_qty;

            // Update positions for seller
			traders.trader_arr[received_order->trader_id].positions[product_idx].qty -= match_qty;
			traders.trader_arr[received_order->trader_id].positions[product_idx].profit += (match_value - match_fee);

			// Update positions for buyer
			traders.trader_arr[buy_cursor->trader_id].positions[product_idx].qty += match_qty;
			traders.trader_arr[buy_cursor->trader_id].positions[product_idx].profit -= match_value;

			// Update buy list
			if(buy_cursor->qty == 0) {
                int removed_buy_level = 0; // The level to be removed from the buy list
                // Check whether removing the order would reduce buy levels
                if(buy_cursor->next == NULL || buy_cursor->price != buy_cursor->next->price) {
                    removed_buy_level = 1;
                }

				if(buy_prev) {
					buy_prev->next = buy_cursor->next;
				} else {
					product_orders->buy_head = buy_cursor->next;
				}

				struct order *temp = buy_cursor;
				buy_cursor = buy_cursor->next;
				free(temp);

                // Update he buy list size and levels
				product_orders->buy_list_size --;
                product_orders->buy_levels -= removed_buy_level;
			}
            // Update exchange fees
            exchange_fees += match_fee;
        } else {
			// No matching order
            // Since the buy list is arranged by price from highest to lowest
			break;
		}
    }

	// Check remaining quantity in the sell order
	if(received_order->qty > 0) {
		// Add the new order node to sell list
        struct order* new_node = (struct order*)malloc(sizeof(struct order));
        memcpy(new_node, received_order, sizeof(struct order));
        new_node->next = NULL;

        int added_sell_level = 1; // The level to be added to the sell list

        // If empty list, add new node as head
        if (product_orders->sell_list_size == 0) {
            product_orders->sell_head = new_node;
        } else {
            // Add new node to sell list in sorted order (low to high)
            struct order* sell_cursor = product_orders->sell_head;  // Set cursor
         	struct order* sell_prev = NULL;  // Set previous node of cursor

            while (sell_cursor) {
				// Check the price
                if (sell_cursor->price <= new_node->price) {
                    // If the sell level exists, no added level
                    if(sell_cursor->price == new_node->price) {
                        added_sell_level = 0;
                    }   
					sell_prev = sell_cursor;
					sell_cursor = sell_cursor->next;
				} else {
                    break;
                }
			}

            // Insert the new node
            if (sell_prev) {
				new_node->next = sell_prev->next;
				sell_prev->next = new_node;
			} else {
				// Insert before the head node as new head
				new_node->next = product_orders->sell_head;
				product_orders->sell_head = new_node;
			}
        }

        // Update the sell list size and levels
        product_orders->sell_list_size++;
        product_orders->sell_levels += added_sell_level;
    }
}


void handle_amend(struct order *received_order, int *fds_exchange) {

    // Delete the old order --- received_order->next
    handle_cancel(received_order, fds_exchange);

    if(received_order->order_type == BUY) {
        // Add the amended order and process it
        handle_buy(received_order, fds_exchange);
    } else if(received_order->order_type == SELL) {
        // Add the amended order and process it
        handle_sell(received_order, fds_exchange);
    }
}

void handle_cancel(struct order* received_order, int *fds_exchange) {
    // Retrive the order to be canceled
    struct order *cancel_order = received_order->next;

    int product_idx = get_productid_by_name(received_order->product, &products);

    // Get the order list for current product
    struct order_list* product_orders = &(order_book[product_idx]);

    if(cancel_order->order_type == BUY) {
        // Cancel buy order
        struct order *buy_cursor = product_orders->buy_head;
        struct order *buy_prev = NULL;
        int removed_level = 0; // The level to be removed from the buy list
        // Delete the order in the buy list
        while(buy_cursor) {
            if(buy_cursor == cancel_order) {
                // Check the removed level if deleting the order 
                if((buy_prev == NULL || buy_prev->price != buy_cursor->price) &&
                    (buy_cursor->next == NULL || buy_cursor->price != buy_cursor->next->price)) {
                    removed_level = 1;
                }
                // If the old order is the head
                if(buy_prev == NULL) {
                    product_orders->buy_head = buy_cursor->next;
                } else {
                    buy_prev->next = buy_cursor->next;
                }

                free(cancel_order);
                product_orders->buy_levels -= removed_level;
                product_orders->buy_list_size --;
                break;
            } else {
                buy_prev = buy_cursor;
                buy_cursor = buy_cursor->next;
            }
        }
    } 
    else if (received_order->order_type == SELL) {
        // Cancel the cell order
        struct order *sell_cursor = product_orders->sell_head;
        struct order *sell_prev = NULL;
        int removed_level = 0; // The level to be removed from the buy list
        // Delet the old order in the buy list
        while(sell_cursor) {
            if(sell_cursor == cancel_order) {
                // Check the removed level if deleting the order 
                if((sell_prev == NULL || sell_prev->price != sell_cursor->price) &&
                    (sell_cursor->next == NULL || sell_cursor->price != sell_cursor->next->price)) {
                    removed_level = 1;
                }

                // If the old order is the head
                if(sell_prev == NULL) {
                    product_orders->sell_head = sell_cursor->next;
                } else {
                    sell_prev->next = sell_cursor->next;
                }

                free(cancel_order);
                product_orders->sell_levels -= removed_level;
                product_orders->sell_list_size --;
                break;
            } else {
                sell_prev = sell_cursor;
                sell_cursor = sell_cursor->next;
            }
        }

    }

}

void notify_filler(int *fds_exchange, int trader_id, int order_id, int fill_qty) {
    if(traders.trader_arr[trader_id].is_alive) {
        char write_buf[BUF_LEN] = {'\0'};
        snprintf(write_buf, BUF_LEN, "FILL %d %d;", order_id, fill_qty);
        write(fds_exchange[trader_id], write_buf, strlen(write_buf));
        kill(traders.trader_arr[trader_id].pid, SIGUSR1);
    }
}

void show_order_book(struct order_list *order_book) {
    printf(LOG_PREFIX"\t--ORDERBOOK--\n");

    for(int i=0; i<products.num_products; i++) {
        printf(LOG_PREFIX"\tProduct: %s; Buy levels: %d; Sell levels: %d\n",products.names[i], order_book[i].buy_levels, order_book[i].sell_levels);

        // Print sell orders (reverse price printing highest to lowest)
        int sell_count = order_book[i].sell_list_size;
        struct order *sell_cursor = order_book[i].sell_head;
        if(sell_cursor) {
            // Reverse list storage, just like a stack
            struct order **sell_stack = (struct order**)malloc(sell_count * sizeof(struct order*));
            int *sell_qtys = (int*)malloc(sell_count * sizeof(int));
            int *sell_counts = (int*)malloc(sell_count * sizeof(int));
            int stack_index = 0;
            int qty_sum_sell = sell_cursor->qty; // The quantity of order products at the same level
            int level_orders_sell = 1; // The number of orders at the same level

            while(sell_cursor->next) {
                if(sell_cursor->price == sell_cursor->next->price) {
                    // Iterate and add the same level quantity
                    sell_cursor = sell_cursor->next;
                    qty_sum_sell += sell_cursor->qty;
                    level_orders_sell ++;
                } else {
                    // Store in stack
                    sell_stack[stack_index] = sell_cursor;
                    sell_qtys[stack_index] = qty_sum_sell;
                    sell_counts[stack_index] = level_orders_sell;
                    // The next count
                    stack_index ++;
                    sell_cursor = sell_cursor->next;
                    qty_sum_sell = sell_cursor->qty;
                    level_orders_sell = 1;
                }
            }
            // Push the final order
            sell_stack[stack_index] = sell_cursor;
            sell_qtys[stack_index] = qty_sum_sell;
            sell_counts[stack_index] = level_orders_sell;
            stack_index ++;

            // Implement reversing printing
            for(int j = stack_index-1; j >= 0; j--) {
                sell_cursor = sell_stack[j];
                if(sell_counts[j] > 1) {
                    printf(LOG_PREFIX"\t\tSELL %d @ $%d (%d orders)\n", sell_qtys[j], sell_cursor->price, sell_counts[j]);
                } else{
                    printf(LOG_PREFIX"\t\tSELL %d @ $%d (%d order)\n", sell_qtys[j], sell_cursor->price, sell_counts[j]);
                }
            }
            free(sell_stack);
            free(sell_qtys);
            free(sell_counts);
        }


        // Print buy orders
        struct order *buy_cursor = order_book[i].buy_head;
        while(buy_cursor) {
            int qty_sum = buy_cursor->qty; // The quantity of order products at the same level
            int level_orders = 1; // The number of orders at the same level
            while(buy_cursor->next && buy_cursor->price == buy_cursor->next->price) {
                // Iterate and add the same level quantity
                buy_cursor = buy_cursor->next;
                qty_sum += buy_cursor->qty;
                level_orders ++;
            }
            if(level_orders > 1) {
                printf(LOG_PREFIX"\t\tBUY %d @ $%d (%d orders)\n", qty_sum, buy_cursor->price, level_orders);
            } else{
                printf(LOG_PREFIX"\t\tBUY %d @ $%d (%d order)\n", qty_sum, buy_cursor->price, level_orders);
            }
            buy_cursor = buy_cursor->next;
        }
    }
}

void show_positions(struct trader_list *traders) {
    printf(LOG_PREFIX"\t--POSITIONS--\n");
    for(int id=0; id<traders->num_traders; id++) {
        printf(LOG_PREFIX"\tTrader %d: ", id);
        for(int i=0; i<products.num_products; i++) {
            char *product_name = traders->trader_arr[id].positions[i].product;
            int owned_qty = traders->trader_arr[id].positions[i].qty;
            long int profit = traders->trader_arr[id].positions[i].profit;
            if(i < products.num_products-1) {
                printf("%s %d ($%ld), ",product_name, owned_qty, profit);
            } else {
                printf("%s %d ($%ld)\n",product_name, owned_qty, profit);
            }
        }
    }
}

void show_trading_end(long int collected_fees) {
    printf(LOG_PREFIX" Trading completed\n");
	printf(LOG_PREFIX" Exchange fees collected: $%ld\n",collected_fees);
}

void free_product_list(struct product_list *products) {
    free(products->names);
}

void free_fds(int *fds_exchange, int *fds_trader) {
    free(fds_exchange);
    free(fds_trader);
}