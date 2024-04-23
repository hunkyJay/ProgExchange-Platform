#ifndef PE_COMMON_H
#define PE_COMMON_H

#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE
#endif
#define _GNU_SOURCE
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define FIFO_EXCHANGE "/tmp/pe_exchange_%d"
#define FIFO_TRADER "/tmp/pe_trader_%d"
#define FEE_PERCENTAGE 1
#define BUF_LEN 128
#define INT_LEN 12
#define PRODUCT_NAME_MAX 17
#define PRODUCT_STR_LEN 16
#define BUY_QTY_MAX 1000
#define MIN_VALUE 1
#define MAX_VALUE 999999
#define BUY_CMD_ARGS 4
#define SELL_CMD_ARGS 4
#define AMEND_CMD_ARGS 3
#define CANCEL_CMD_ARGS 1
#define MARKET_SELL_ARGS 3
#define ACCEPTED_ARGS 1
#define STR_HELPER(x) #x
#define TO_STRING(x) STR_HELPER(x) 

enum OrderType { 
    BUY, 
    SELL, 
    // AMEND, 
    // CANCEL, 
    INVALID_ORDER 
}; // The order type enum

enum OrderResponseType { 
    ACCEPTED, 
    AMENDED, 
    CANCELLED, 
    INVALID,
    NORESPONSE
}; // The market response type enum

struct product_list {
    int num_products;
    char (*names)[PRODUCT_NAME_MAX];
}; // The list of products

struct order {
    int trader_id;
    enum OrderType order_type;
    int order_id;
    char product[PRODUCT_NAME_MAX];
    int qty;
    int price;
    struct order* next;
}; // The order node structure

struct order_list {
    struct order* buy_head;
    struct order* sell_head;
    int buy_list_size;
    int sell_list_size;
    int buy_levels;
    int sell_levels;
}; // The list of buy and sell orders

struct position {
    char product[PRODUCT_NAME_MAX];
    int qty;
    long int profit;
}; // The position of a product

struct trader {
    int id;
    char *name;
    int num_orders;
    int pid;
    int is_alive;
    struct position *positions;
}; // The trader structure

#endif
