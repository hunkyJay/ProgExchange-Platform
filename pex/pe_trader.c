#include "pe_trader.h"

volatile sig_atomic_t sigusr1_received = 0;
volatile sig_atomic_t sigpipe_received = 0;

void auto_trader_handler(int sig) {
    if (sig == SIGUSR1) {
        sigusr1_received = 1;
    } else if (sig == SIGPIPE) {
        sigpipe_received = 1;
    }
}

int main(int argc, char ** argv) {
    // implement your trader program to be fault-tolerant.
    if (argc < 2) {
        printf("Not enough arguments\n");
        return 1;
    }

    int trader_id = atoi(argv[1]);

    // register signal handler
    struct sigaction sa;
    sa.sa_handler = auto_trader_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if(sigaction(SIGUSR1, &sa, NULL) == -1){
        perror("Failed to register sigusr1");
        exit(1);
    }
    if(sigaction(SIGPIPE, &sa, NULL) == -1) {
        perror("Failed to register sigpipe");
        exit(1);
    }

    // connect to named pipes
    char fifo_trader[BUF_LEN];
    char fifo_exchange[BUF_LEN];
    snprintf(fifo_trader, sizeof(fifo_trader), FIFO_TRADER, trader_id);
    snprintf(fifo_exchange, sizeof(fifo_exchange), FIFO_EXCHANGE, trader_id);

    int fd_exchange = open(fifo_exchange, O_RDONLY);
    if (fd_exchange == -1) {
        perror("Failed to open fifo_exchange");
        return 1;
    }
    int fd_trader = open(fifo_trader, O_WRONLY);
    if (fd_trader == -1) {
        perror("Failed to open fifo_trader");
        return 1;
    }

    int order_id = 0; // Initialize the order id
    char product[PRODUCT_NAME_MAX] = {'\0'};
    int qty, price;
    time_t send_order_time = 0; // Initialize the order time


    // event loop:
    while (1) {
        // wait for exchange update (MARKET message)
        pause();
        if (sigusr1_received) {
            sigusr1_received = 0;
            // Read message from exchange market
            char read_buf[BUF_LEN] = {'\0'};
            size_t read_len = read(fd_exchange, read_buf, BUF_LEN - 1);
            if (read_len > 0) {
                read_buf[read_len] = '\0';

                // MARKET SELL message from exchange
                if (strstr(read_buf, "MARKET SELL")) {
                    int read_ret = sscanf(read_buf, "MARKET SELL %s %d %d;", product, &qty, &price);
                    // Error handling
                    if (read_ret != MARKET_SELL_ARGS) {
                        continue;
                    }

                    if (strlen(product) == 0) {
                        continue;
                    }

                    if (qty < MIN_VALUE) {
                        continue;
                    }

                    if(price < MIN_VALUE || price > MAX_VALUE) {
                        continue;
                    }

                    // Check the quantity maximum 1000
                    if (qty >= BUY_QTY_MAX) {
                        break;
                    }

                    // Send order to exchange
                    char write_buf[BUF_LEN] = {'\0'};
                    snprintf(write_buf, BUF_LEN, "BUY %d %s %d %d;", order_id, product, qty, price);
                    ssize_t write_len = write(fd_trader, write_buf, strlen(write_buf));
                    if(write_len < 0 && errno == EPIPE) {
                        perror("Failed to write to fifo_trader");
                        break;
                    }
                    kill(getppid(), SIGUSR1);

                    // Update the order time
                    send_order_time = time(NULL);
                }
                // ACCEPTED ORDERID message from exchange
                else if (strstr(read_buf, "ACCEPTED")) {
                    int accepted_id;
                    int read_ret = sscanf(read_buf, "ACCEPTED %d;", &accepted_id);
                    if(read_ret != ACCEPTED_ARGS) {
                        continue;
                    }
                    if(accepted_id == order_id) {
                        order_id++;
                        send_order_time = 0; // Reset order time to 0 after accepted
                    }
                }
                // The order was invalid since amending or something else
                else if (strstr(read_buf, "INVALID")) {
                    send_order_time = 0; // Reset order time to 0 after rejected
                    continue;
                }
            }
        }
        // Check the exchange disconnect
        if(sigpipe_received) {
            break;
        }

        // If no response after timeout, resend the order
        if (send_order_time != 0 && time(NULL) - send_order_time > TIMEOUT) {
            char write_buf[BUF_LEN] = {'\0'};
            snprintf(write_buf, BUF_LEN, "BUY %d %s %d %d;", order_id, product, qty, price);
            ssize_t write_len = write(fd_trader, write_buf, strlen(write_buf));
            if (write_len < 0 && errno == EPIPE) {
                perror("Failed to write to fifo_trader");
                break;
            }
            kill(getppid(), SIGUSR1);

            // Update the order time
            send_order_time = time(NULL);
        }
    }

        close(fd_trader);
        close(fd_exchange);

        return 0;
}

