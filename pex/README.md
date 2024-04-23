### 1. The exchange implementation

 - The exchange program reads the products file and the traders as command line args for initialization, launching the trader as a child process, and creating two named pipes for each trader respectively to connect.
 - Exchange listens for two kinds of signals: SIGUSR1, which is used to communicate with traders, and SIGCHILD, which is used to detect whether a trader has terminated (disconnected) and to update the number of connected traders.
- Exchange runs as event loop and can process orders from multiple traders, which is based on a PID circular queue. Every time the exchange receives a signal from a trader, it adds that trader's PID to the queue. Then, in each iteration, the exchange takes a PID from the queue and processes that trader's order. 
 - When there is no alive trader process, the exchange closes and prints an end message.

  
#### The order processing process is as follows

``` 
+------------+                  +---------------+       +--------------+      +-----------------+
|            |  Accepted/Notice |               |       |              |      |                  |     
|Parse command|---------------->|Process command |---->|Send response  | ---->|Notify fillers 
|            |                  |               |       |              |      |                  |     
+------------+                  +---------------+       +--------------+      +-----------------+
       ^               +----------------------------+                                                    
       |               |  PID Circular Queue        |                                         
       +---------------|							|
					   +----------------------------+ 
                            Order processing flow
                                    
```


### 2. The fault-tolerant trader

- For my auto trader program, given that it should only respond to sell orders, I think it is important to listen for the SELL command and the ACCEPTED/INVALID command, and for any exceptions to the exchange, such as disconnection or signal lost
- My trader registered two kinds of signals: SIGUSR1 for mutual notification with exchange, and SIGPIPE for monitoring exchange for closing or disconnecting pipes.
- For fault tolerance, the program can avoid undefined behavior by error message checking and error handling, as well as handling pipe exceptions. In addition, I set the timeout of the transaction so that the trader, upon receiving the "ACCEPTED" or "INVALID" message from the exchange, will reset the sending time of the order to make it easier to track the status of the order. If no response is received after the timeout period, the trader automatically resends the order until either a valid response is received or an exchange disconnection is detected.


### 3. Testing
I have tested the exchange with both unit tests and end-to-end tests

#### Unit Tests
- The unit tests focus on the exchange basic functionalities such as initialization and buy/sell/amend/cancel orders.

#### End-to-end tests
- The end-to-end tests is using the test traders under the E2E directory to test the exchange behaviours

#### How to test
- To compile the test files, using
```
$ make tests
```

- Then, to run the tests
```
$ make run_tests
```




