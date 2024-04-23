#ifndef PE_TRADER_H
#define PE_TRADER_H

#include "pe_common.h"
#include <time.h>

#define TIMEOUT 2

/**
 * Handle the message signals from the exchange market
 * @param sig The received signal number
 */
void auto_trader_handler(int sig);


#endif