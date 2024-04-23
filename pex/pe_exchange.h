#ifndef PE_EXCHANGE_H
#define PE_EXCHANGE_H

#include "pe_common.h"

#define LOG_PREFIX "[PEX]"
#define QUEUE_SIZE_BASE 8

struct trader_list {
    int num_traders;
    struct trader *trader_arr;
}; // The list of traders

// Circular queue to store pids
// Reference: https://edstem.org/au/courses/10466/discussion/1353883,
// https://www.programiz.com/dsa/circular-queue
struct pid_circular_queue {
    int *pid_arr;
    int front;
    int rear;
    int max_size;
};

/**
 * Initialize a circular queue to store pids
 * @param pid_queue The pointer to the pid_queue to initialize
 * @param size The size of the circular queue
 */
void init_pid_queue(struct pid_circular_queue *pid_queue, int size);

/**
 * Check whether the circular queue is empty
 * @param pid_queue The pointer to the pid_queue to check
 * @return int True if empty, false otherwise
 */
int is_empty_queue(struct pid_circular_queue *pid_queue);

/**
 * Add a pid to the rear of the circular queue
 * @param pid_queue The pointer to the pid_queue
 * @param pid The pid to add
 */
void pid_enqueue(struct pid_circular_queue *pid_queue, int pid);

/**
 * Get a pid from the front of the circular queue
 * @param pid_queue The pointer to the pid_queue
 * @return int The pid to retrieve
 */
int pid_dequeue(struct pid_circular_queue *pid_queue);

/**
 * Free the allocated space in the circular queue
 * @param pid_queue The pointer to the pid_queue
 */
void free_pid_queue(struct pid_circular_queue *pid_queue);

/**
 * Handle the communication signal from the traders
 * @param sig The received signal number
 * @param info The information about the signal, e.g the pid of the sender
 * @param ucontext The context or state information when executing program
 */
void exchange_handler(int sig, siginfo_t* info, void* ucontext);

/**
 * Handle the disconnected signal from the traders when the trader is terminated
 * @param sig The received signal number
 * @param info The information about the signal, e.g the pid of the sender
 * @param ucontext The context or state information when executing program
 */
void trader_disconnect_handler(int sig, siginfo_t* info, void* ucontext);

/**
 * Read the products infomation form the given file
 * @param filename The product file to read
 * @param products The product list to store the information, including names and num of types
 */
void read_product_file (const char *filename, struct product_list* products);

/**
 * Initialize a list of traders in the exchange
 * @param num_traders The number of traders to register
 * @param trader_names The trader names string array
 * @param products The products list in the exchange, including product names and num of types
 * @return The trader_list structure of initialized traders
 */
struct trader_list init_traders (int num_traders, char **trader_names, struct product_list *products);

/**
 * Free the memory allocated for the trader list
 * @param traders The trader list to free
 */
void free_traders(struct trader_list *traders);

/**
 * Get a trader id by its pid from the trader list
 * @param traders The pointer to the trader list
 * @param pid The pid of the trader
 * @return int The id of the trader if found, -1 otherwise
 */
int get_traderid_by_pid(struct trader_list *traders, int pid);

/**
 * Initialize the order book in the exchange, which includes the buy/sell order lists of each product
 * @param num_products The number of product types
 * @return The order_list structure array
 */
struct order_list* init_order_book(int num_products);

/**
 * Free the memory allocated for the order list
 * @param head The head node of the order list
 */
void free_order_list(struct order *head);

/**
 * Free the memory allocated for the order book including buy/sell order lists
 * @param order_book The order_book to free
 * @param num_products The number of product types
 */
void free_order_book(struct order_list *order_book, int num_products);

/**
 * Print the pex starting information with market products
 * @param products The list of products in exchange
 */
void show_pex_start(struct product_list *products);

/**
 * Make fifos and connect to the named pipes for exchange and traders
 * @param fds_exchange The pointer to the exchange fds array for writing
 * @param fds_trader The pointer to the trader fds array for reading
 * @param traders The pointer to the trader list including all traders and number of traders
 */
void make_connect_fifos(int **fds_exchange, int **fds_trader, struct trader_list *traders);

/**
 * Launch a trader as child processes of exchange
 * @param traders The list of traders in exchange
 * @param trader_id The id of the trader
 */
void launch_trader(struct trader_list *traders, int trader_id);

/**
 * Exchange market open, and send open message to traders
 * @param fds_exchange The exchange fds to write
 * @param traders The list of traders in exchange
 */
void market_open_msg(int* fds_exchange, struct trader_list* traders);

/**
 * Get a trader index by its name from the product list
 * @param product_name The name of the product
 * @param products The pointer to the product list
 * @return int The index of the product if found, -1 otherwise
 */
int get_productid_by_name(char* product_name, struct product_list* products);

/**
 * Parse the command received from trader
 * @param fds_trader The trader fds to read from
 * @param trader_id The id of the trader that sends the message
 * @param received_order The order of the message after parsing the command
 * @return enum OrderResponseType The type of the exchange response
 */
enum OrderResponseType parse_command(int *fds_trader, int trader_id, struct order *received_order);

/**
 * Check whether the buy command is invalid
 * @param command The received command string
 * @param trader_id The id of the trader that sends the message
 * @param received_order The order of the message after parsing the command
 * @return int True 1 if it is a valid buy command, false 0 otherwise
 */
int is_valid_buy(char *command, int trader_id, struct order *received_order);

/**
 * Check whether the sell command is invalid
 * @param command The received command string
 * @param trader_id The id of the trader that sends the message
 * @param received_order The order of the message after parsing the command
 * @return int True 1 if it is a valid sell command, false 0 otherwise
 */
int is_valid_sell(char *command, int trader_id, struct order *received_order);

/**
 * Check whether the amend command is invalid
 * @param command The received command string
 * @param trader_id The id of the trader that sends the message
 * @param received_order The order of the message after parsing the command
 * @return int True 1 if it is a valid sell command, false 0 otherwise
 */
int is_valid_amend(char *command, int trader_id, struct order *received_order);

/**
 * Check whether the cancel command is invalid
 * @param command The received command string
 * @param trader_id The id of the trader that sends the message
 * @param received_order The order of the message after parsing the command
 * @return int True 1 if it is a valid sell command, false 0 otherwise
 */
int is_valid_cancel(char *command, int trader_id, struct order *received_order);

/**
 * Send response to current order message sender
 * @param response The response type for the order message
 * @param fds_exchange The exchange fds to write
 * @param received_order The order in the message received after parsing the command
 */
void send_order_response(enum OrderResponseType response, int *fds_exchange, struct order *received_order);

/**
 * Notify other traders in the exchange with the latest order message
 * @param response The response type for the order message
 * @param fds_exchange The exchange fds to write
 * @param received_order The order in the message received after parsing the command
 */
void notify_traders(enum OrderResponseType response, int *fds_exchange, struct order *received_order);

/**
 * Process the order read from trader message
 * @param response The response type for the order message
 * @param fds_exchange The exchange fds to write
 * @param received_order The order in the message received after parsing the command
 */
void process_order(enum OrderResponseType response, int *fds_exchange, struct order *received_order);

/**
 * Process the buy order command in the exchange
 * @param received_order The order in the message received after parsing the command
 * @param fds_exchange The exchange fds to write
 */
void handle_buy(struct order* received_order, int *fds_exchange);

/**
 * Process the sell order command in the exchange
 * @param received_order The order in the message received after parsing the command
 * @param fds_exchange The exchange fds to write
 */
void handle_sell(struct order* received_order, int *fds_exchange);

/**
 * Process the amend order command in the exchange
 * @param received_order The order in the message received after parsing the command
 * @param fds_exchange The exchange fds to write
 */
void handle_amend(struct order* received_order, int *fds_exchange);

/**
 * Process the cancel order command in the exchange
 * @param received_order The order in the message received after parsing the command
 * @param fds_exchange The exchange fds to write
 */
void handle_cancel(struct order* received_order, int *fds_exchange);

/**
 * Send the notifing message to the trader when the order has been filled
 * @param fds_exchange The exchange fds to write
 * @param trader_id The id of the trader
 * @param order_id The id of the order that has been filled
 * @param fill_qty The quantity of the products that have been filled in the order
 */
void notify_filler(int *fds_exchange, int trader_id, int order_id, int fill_qty);

/**
 * Print the order book information in the exchange
 * @param order_book The order book including the product order lists
 */
void show_order_book(struct order_list *order_book);

/**
 * Print the positions information of each trader in the exchange
 * @param traders The trader list including the all traders
 */
void show_positions(struct trader_list *traders);

/**
 * Print the end information of the exchange, trading completed with the collected fees
 * @param collected_fees The exchange fees collected
 */
void show_trading_end(long int collected_fees);

/**
 * Free the allocated memory for the product list
 * @param products The product list
 */
void free_product_list(struct product_list* products);

/**
 * Free the allocated memory for the exchange fds and trader fds
 * @param fds_exchange The exchange fds
 * @param fds_trader The trader fds
 */
void free_fds(int *fds_exchange, int* fds_trader);

#endif