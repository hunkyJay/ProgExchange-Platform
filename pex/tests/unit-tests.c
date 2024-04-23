// TODO
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include "cmocka.h"
#include "../pe_exchange.h"

extern struct product_list products;
extern struct trader_list traders;
extern struct order_list *order_book;

static int setup() {
    read_product_file("products.txt", &products);
    char* trader_names[] = {"a", "b", "c"};
    traders = init_traders(3, trader_names, &products);
    order_book = init_order_book(products.num_products);

    return 0;
}

static int teardown() {
    free_order_book(order_book, products.num_products);
    free_product_list(&products);
    free_traders(&traders);

    return 0;
}

static void test_read_product_file() {
    // Read from the products file
    read_product_file("products.txt", &products);
    assert_int_equal(products.num_products, 2);
    assert_string_equal(products.names[0], "GPU");
    assert_string_equal(products.names[1], "Router");

    // Get product id by name
    assert_int_equal(get_productid_by_name("GPU", &products), 0);
    free_product_list(&products);
}

static void test_pid_queue() {
    // Init pid queue
    struct pid_circular_queue pid_queue;
    init_pid_queue(&pid_queue, 3);
    assert_int_equal(pid_queue.max_size, 3);
    assert_int_equal(pid_queue.front, -1);
    assert_int_equal(pid_queue.rear, -1);

    // Check empty
    assert_int_equal(is_empty_queue(&pid_queue), 1);

    // Enqueue
    pid_enqueue(&pid_queue, 1);
    assert_int_equal(pid_queue.front, 0);
    assert_int_equal(pid_queue.rear, 0);
    assert_int_equal(is_empty_queue(&pid_queue), 0);

    pid_enqueue(&pid_queue, 2);
    assert_int_equal(pid_queue.front, 0);
    assert_int_equal(pid_queue.rear, 1);
    assert_int_equal(is_empty_queue(&pid_queue), 0);

    pid_enqueue(&pid_queue, 3);
    assert_int_equal(pid_queue.front, 0);
    assert_int_equal(pid_queue.rear, 2);

    pid_enqueue(&pid_queue, 4);
    assert_int_equal(pid_queue.front, 1);
    assert_int_equal(pid_queue.rear, 0);

    // Dequque
    assert_int_equal(pid_dequeue(&pid_queue), 2);
    assert_int_equal(pid_queue.front, 2);
    assert_int_equal(pid_queue.rear, 0);

    assert_int_equal(pid_dequeue(&pid_queue), 3);
    assert_int_equal(pid_queue.front, 0);
    assert_int_equal(pid_queue.rear, 0);

    assert_int_equal(pid_dequeue(&pid_queue), 4);
    assert_int_equal(pid_queue.front, -1);
    assert_int_equal(pid_queue.rear, -1);
    assert_int_equal(is_empty_queue(&pid_queue), 1);

    free_pid_queue(&pid_queue);
}

static void test_init_traders() {
    char* trader_names[] = {"a", "b", "c"};
    traders = init_traders(3, trader_names, &products);
    traders.trader_arr[2].pid = 5;

    assert_int_equal(traders.num_traders, 3);
    assert_string_equal(traders.trader_arr[0].name, "a");
    assert_string_equal(traders.trader_arr[1].name, "b");
    assert_string_equal(traders.trader_arr[2].name, "c");
    assert_int_equal(get_traderid_by_pid(&traders,5), 2);
    free_traders(&traders);
}

static void test_init_order_book() {
    order_book = init_order_book(2);
    assert_null(order_book[1].buy_head);
    assert_null(order_book[1].sell_head);
    assert_int_equal(order_book[1].buy_levels, 0);
    assert_int_equal(order_book[1].buy_list_size, 0);
    assert_int_equal(order_book[1].sell_levels, 0);
    assert_int_equal(order_book[1].sell_list_size, 0);

    free_order_book(order_book, 2);
}

static void test_buy_command() {
    struct order received_order;

    // Invalid buys
    char* command_2 = "BUY GPU 20 20";
    assert_false(is_valid_buy(command_2, 0, &received_order));

    char* command_3 = "BUY 0 SCREEN 20 20";
    assert_false(is_valid_buy(command_3, 0, &received_order));

    char* command_4 = "BUY -1 GPU 20 20";
    assert_false(is_valid_buy(command_4, 0, &received_order));

    char* command_5 = "BUY 0 GPU 0 20";
    assert_false(is_valid_buy(command_5, 0, &received_order));

    char* command_6 = "BUY 0 GPU 20 0";
    assert_false(is_valid_buy(command_6, 0, &received_order));

    char* command_7 = "BUY 1999999 GPU 20 20";
    assert_false(is_valid_buy(command_7, 0, &received_order));

    // A valid buy
    char* command_1 = "BUY 0 GPU 20 20";
    assert_true(is_valid_buy(command_1, 0, &received_order));

    int *empty_fds = NULL;
    handle_buy(&received_order, empty_fds);
    int product_id = get_productid_by_name(received_order.product, &products);

    assert_int_equal(order_book[product_id].buy_list_size, 1);
    assert_int_equal(order_book[product_id].buy_levels, 1);

}

static void test_sell_command() {
    struct order received_order;

    // Invalid buys
    char* command_2 = "SELL GPU 20 20";
    assert_false(is_valid_sell(command_2, 0, &received_order));

    char* command_3 = "SELL 0 SCREEN 20 20";
    assert_false(is_valid_sell(command_3, 0, &received_order));

    char* command_4 = "SELL -1 GPU 20 20";
    assert_false(is_valid_sell(command_4, 0, &received_order));

    char* command_5 = "SELL 0 GPU 0 20";
    assert_false(is_valid_sell(command_5, 0, &received_order));

    char* command_6 = "SELL 0 GPU 20 0";
    assert_false(is_valid_sell(command_6, 0, &received_order));

    char* command_7 = "SELL 1999999 GPU 20 20";
    assert_false(is_valid_sell(command_7, 0, &received_order));

    // A valid sell
    char* command_1 = "SELL 0 GPU 20 20";
    assert_true(is_valid_sell(command_1, 0, &received_order));

    int* empty_fds = NULL;
    handle_sell(&received_order, empty_fds);
    int product_id = get_productid_by_name(received_order.product, &products);

    assert_int_equal(order_book[product_id].sell_list_size, 1);
    assert_int_equal(order_book[product_id].sell_levels, 1);

}

static void test_amend_command() {
    struct order received_order;

    // Invalid amends
    char* command_0 = "AMEND 0 1 1";
    assert_false(is_valid_amend(command_0, 0, &received_order));

    char* command_2 = "AMEND 0 1";
    assert_false(is_valid_amend(command_2, 0, &received_order));

    char* command_3 = "AMEND -1 1 1";
    assert_false(is_valid_amend(command_3, 0, &received_order));

    traders.trader_arr[0].num_orders = 1;
    char* command_4 = "AMEND 0 0 1";
    assert_false(is_valid_amend(command_4, 0, &received_order));

    char* command_5 = "AMEND 0 1 0";
    assert_false(is_valid_amend(command_5, 0, &received_order));

    char* command_6 = "AMEND 0 1999999 1";
    assert_false(is_valid_amend(command_6, 0, &received_order));

    char* command_7 = "AMEND 0 1 1999999";
    assert_false(is_valid_amend(command_7, 0, &received_order));

    // A valid amend
    traders.trader_arr[0].num_orders = 0;
    char* command_sell = "SELL 0 GPU 20 20";
    assert_true(is_valid_sell(command_sell, 0, &received_order));

    int* empty_fds = NULL;
    handle_sell(&received_order, empty_fds);
    int product_id = get_productid_by_name(received_order.product, &products);
    traders.trader_arr[0].num_orders++;

    char* command_1 = "AMEND 0 10 10";

    assert_true(is_valid_amend(command_1, 0, &received_order));

    handle_amend(&received_order, empty_fds);
    assert_int_equal(order_book[product_id].sell_list_size, 1);
    assert_int_equal(order_book[product_id].sell_levels, 1);
    assert_int_equal(order_book[product_id].sell_head->price, 10);
    assert_int_equal(order_book[product_id].sell_head->qty, 10);

}

static void test_cancel_command() {
    struct order received_order;

    // Invalid amends
    char* command_0 = "CANCEL 0 1 1";
    assert_false(is_valid_cancel(command_0, 0, &received_order));

    char* command_2 = "CANCEL ";
    assert_false(is_valid_cancel(command_2, 0, &received_order));

    char* command_3 = "CANCEL-1";
    assert_false(is_valid_cancel(command_3, 0, &received_order));

    char* command_4 = "CANCEL 0";
    assert_false(is_valid_cancel(command_4, 0, &received_order));

    traders.trader_arr[0].num_orders = 1;
    char* command_5 = "CANCEL 0";
    assert_false(is_valid_cancel(command_5, 0, &received_order));

    char* command_6 = "CANCEL 1999999";
    assert_false(is_valid_cancel(command_6, 0, &received_order));


    // A valid cancel
    traders.trader_arr[0].num_orders = 0;
    char* command_sell = "SELL 0 GPU 20 20";
    assert_true(is_valid_sell(command_sell, 0, &received_order));

    int* empty_fds = NULL;
    handle_sell(&received_order, empty_fds);
    int product_id = get_productid_by_name(received_order.product, &products);
    traders.trader_arr[0].num_orders++;

    assert_int_equal(order_book[product_id].sell_list_size, 1);
    assert_int_equal(order_book[product_id].sell_levels, 1);
    assert_non_null(order_book[product_id].sell_head);

    char* command_1 = "CANCEL 0";
    assert_true(is_valid_cancel(command_1, 0, &received_order));

    handle_cancel(&received_order, empty_fds);
    assert_int_equal(order_book[product_id].sell_list_size, 0);
    assert_int_equal(order_book[product_id].sell_levels, 0);
    assert_null(order_book[product_id].sell_head);

}

static void test_match() {
    struct order received_order;

    // A valid buy1
    char* command_buy1 = "BUY 0 GPU 20 20";
    assert_true(is_valid_buy(command_buy1, 0, &received_order));

    int* empty_fds = NULL;
    handle_buy(&received_order, empty_fds);
    int product_id = get_productid_by_name(received_order.product, &products);
    traders.trader_arr[0].num_orders++;

    // A valid buy2
    char* command_buy2 = "BUY 1 GPU 20 10";
    assert_true(is_valid_buy(command_buy2, 0, &received_order));

    handle_buy(&received_order, empty_fds);
    traders.trader_arr[0].num_orders++;

    // A valid sell1 to fill buy1
    char* command_sell1 = "SELL 0 GPU 20 20";
    assert_true(is_valid_sell(command_sell1, 1, &received_order));

    handle_sell(&received_order, empty_fds);
    traders.trader_arr[1].num_orders++;

    assert_int_equal(order_book[product_id].sell_list_size, 0);
    assert_int_equal(order_book[product_id].sell_levels, 0);
    assert_null(order_book[product_id].sell_head);

    assert_int_equal(order_book[product_id].buy_list_size, 1);
    assert_int_equal(order_book[product_id].buy_levels, 1);
    assert_non_null(order_book[product_id].buy_head);

    // A valid sell2 to particially fill buy2
    char* command_sell2 = "SELL 1 GPU 10 10";
    assert_true(is_valid_sell(command_sell2, 1, &received_order));

    handle_sell(&received_order, empty_fds);
    traders.trader_arr[1].num_orders++;

    assert_int_equal(order_book[product_id].sell_list_size, 0);
    assert_int_equal(order_book[product_id].sell_levels, 0);
    assert_null(order_book[product_id].sell_head);

    assert_int_equal(order_book[product_id].buy_list_size, 1);
    assert_int_equal(order_book[product_id].buy_levels, 1);
    assert_non_null(order_book[product_id].buy_head);
    assert_int_equal(order_book[product_id].buy_head->qty, 10);
}


int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_read_product_file),
        cmocka_unit_test(test_pid_queue),
        cmocka_unit_test(test_init_traders),
        cmocka_unit_test(test_init_order_book),
        cmocka_unit_test_setup_teardown(test_buy_command, setup, teardown),
        cmocka_unit_test_setup_teardown(test_sell_command, setup, teardown),
        cmocka_unit_test_setup_teardown(test_amend_command, setup, teardown),
        cmocka_unit_test_setup_teardown(test_cancel_command, setup, teardown),
        cmocka_unit_test_setup_teardown(test_match, setup, teardown)
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}