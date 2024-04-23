#include "../../pe_trader.h"

volatile sig_atomic_t sigusr1_received = 0;

void auto_trader_handler(int sig) {
    sigusr1_received = 1;
}

int main(int argc, char** argv) {
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
    if(sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("Failed to register sigusr1");
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

    
    // Wait for the market open
    sleep(5);

    char* buy_1 = "BUY 0 GPU 15 150;";
    write(fd_trader, buy_1, strlen(buy_1));
    kill(getppid(), SIGUSR1);

    sleep(3);
    char* buy_2 = "BUY 1 Router 10 100;";
    write(fd_trader, buy_2, strlen(buy_2));
    kill(getppid(), SIGUSR1);

    sleep(3);
    char* buy_3 = "BUY 2 Router 10 100;";
    write(fd_trader, buy_3, strlen(buy_3));
    kill(getppid(), SIGUSR1);

    sleep(3);
    char* buy_4 = "BUY 3 GPU 20 200;";
    write(fd_trader, buy_4, strlen(buy_4));
    kill(getppid(), SIGUSR1);

    sleep(3);
    char* sell_1 = "SELL 4 GPU 20 130;";
    write(fd_trader, sell_1, strlen(sell_1));
    kill(getppid(), SIGUSR1);

    sleep(3);
    char* sell_2 = "SELL 5 GPU 20 500";

    write(fd_trader, sell_2, strlen(sell_2));
    kill(getppid(), SIGUSR1);

    sleep(3);
    char* sell_3 = "SELL 5 Router 20 100;";
    write(fd_trader, sell_3, strlen(sell_3));
    kill(getppid(), SIGUSR1);

    sleep(5);

    close(fd_trader);
    close(fd_exchange);

    return 0;
}
