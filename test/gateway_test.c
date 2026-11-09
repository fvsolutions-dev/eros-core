/* Gateway endpoints: a host's source realm is set where its frame enters, a pool endpoint serves
 * a range of realms (one per host connection), and answers reach only the host that asked.
 */

#include <string.h>

#include "eros.h"
#include "unity.h"

#define ENDPOINTS_COUNT 16
#define QUEUE_SIZE      8
#define ROUTER_REALM    0
#define SERVER_ID       2
#define POOL_FIRST      5
#define POOL_COUNT      4
#define OTHER_REALM     1

// What an unbuffered gateway endpoint was handed last, its header included
static uint8_t g_callback_frame[64];
static size_t  g_callback_size;

void setUp(void)    { g_callback_size = 0; }
void tearDown(void) {}

/** @brief A frame as a host sends it: the 3-byte header, then the data. */
static size_t make_frame(uint8_t *frame, uint8_t target_id, uint8_t target_realm,
                         uint8_t source_id, uint8_t source_realm, uint8_t sequence,
                         const char *data)
{
    size_t data_size = strlen(data);

    frame[0] = (uint8_t)(target_id << 4 | target_realm);
    frame[1] = (uint8_t)(source_id << 4 | source_realm);
    frame[2] = sequence;
    memcpy(frame + 3, data, data_size);
    return 3 + data_size;
}

/** @brief Every package waiting on a buffered endpoint, dropped. */
static void drain(eros_endpoint_t *endpoint)
{
    eros_package_t *package;

    while ((package = eros_buffered_endpoint_receive(endpoint, 0)) != NULL) {
        eros_package_delete(package);
    }
}

/** @brief The unbuffered gateway endpoint's callback: the frame kept for the test. */
static void gateway_callback(eros_endpoint_t *endpoint, uint8_t *data, size_t size)
{
    (void)endpoint;
    memcpy(g_callback_frame, data, size);
    g_callback_size = size;
}

void test_ingest_sets_the_hosts_source_realm(void)
{
    eros_router_t *router = eros_router_new(ROUTER_REALM, ENDPOINTS_COUNT);
    eros_endpoint_t *server = eros_buffered_endpoint_new(SERVER_ID, router, QUEUE_SIZE);
    eros_endpoint_t *gateway = eros_buffered_gateway_endpoint_new(14, router, QUEUE_SIZE, 5);
    uint8_t frame[16];

    eros_router_register_endpoint(router, server);
    eros_router_register_endpoint(router, gateway);

    // The host writes realm 9; it is in realm 5
    size_t size = make_frame(frame, SERVER_ID, ROUTER_REALM, 1, 9, 3, "hi");
    TEST_ASSERT_EQUAL(0, eros_endpoint_gateway_ingest(gateway, frame, size, 0));

    eros_package_t *package = eros_buffered_endpoint_receive(server, 100);
    TEST_ASSERT_NOT_NULL(package);
    TEST_ASSERT_EQUAL(1, package->source.id);
    TEST_ASSERT_EQUAL(5, package->source.realm_id);
    TEST_ASSERT_EQUAL(3, package->sequence_number);
    TEST_ASSERT_EQUAL(2, package->size);
    TEST_ASSERT_EQUAL_MEMORY("hi", package->data, 2);
    eros_package_delete(package);

    eros_endpoint_delete(gateway);
    eros_endpoint_delete(server);
    eros_router_delete(router);
}

void test_pool_refuses_a_realm_it_does_not_serve(void)
{
    eros_router_t *router = eros_router_new(ROUTER_REALM, ENDPOINTS_COUNT);
    eros_endpoint_t *server = eros_buffered_endpoint_new(SERVER_ID, router, QUEUE_SIZE);
    eros_endpoint_t *pool =
        eros_buffered_gateway_pool_new(11, router, QUEUE_SIZE, POOL_FIRST, POOL_COUNT);
    uint8_t frame[16];

    eros_router_register_endpoint(router, server);
    eros_router_register_endpoint(router, pool);
    size_t size = make_frame(frame, SERVER_ID, ROUTER_REALM, 1, 0, 0, "x");

    // Below, above and past the range: refused, nothing routed
    TEST_ASSERT_EQUAL(-1, eros_endpoint_gateway_ingest_from(pool, POOL_FIRST - 1, frame, size, 0));
    TEST_ASSERT_EQUAL(-1, eros_endpoint_gateway_ingest_from(pool, POOL_FIRST + POOL_COUNT, frame,
                                                            size, 0));
    TEST_ASSERT_EQUAL(-1, eros_endpoint_gateway_ingest_from(pool, 16, frame, size, 0));
    TEST_ASSERT_EQUAL(0, eros_port_queue_count(server->endpoint.buffered_endpoint.queue));

    // The last realm of the range: taken
    TEST_ASSERT_EQUAL(0, eros_endpoint_gateway_ingest_from(pool, POOL_FIRST + POOL_COUNT - 1,
                                                           frame, size, 0));
    TEST_ASSERT_EQUAL(1, eros_port_queue_count(server->endpoint.buffered_endpoint.queue));

    drain(server);
    eros_endpoint_delete(pool);
    eros_endpoint_delete(server);
    eros_router_delete(router);
}

void test_answers_reach_only_the_host_that_asked(void)
{
    eros_router_t *router = eros_router_new(ROUTER_REALM, ENDPOINTS_COUNT);
    eros_endpoint_t *server = eros_buffered_endpoint_new(SERVER_ID, router, QUEUE_SIZE);
    eros_endpoint_t *pool =
        eros_buffered_gateway_pool_new(11, router, QUEUE_SIZE, POOL_FIRST, POOL_COUNT);
    eros_endpoint_t *other = eros_buffered_gateway_endpoint_new(12, router, QUEUE_SIZE, OTHER_REALM);
    uint8_t frame[16];

    eros_router_register_endpoint(router, server);
    eros_router_register_endpoint(router, pool);
    eros_router_register_endpoint(router, other);

    // Two hosts with the same id and sequence, on the pool's second and third realm
    size_t size = make_frame(frame, SERVER_ID, ROUTER_REALM, 1, 0, 5, "q");
    TEST_ASSERT_EQUAL(0, eros_endpoint_gateway_ingest_from(pool, POOL_FIRST + 1, frame, size, 0));
    TEST_ASSERT_EQUAL(0, eros_endpoint_gateway_ingest_from(pool, POOL_FIRST + 2, frame, size, 0));

    // The server answers each request to its sender
    for (int i = 0; i < 2; i++) {
        eros_package_t *request = eros_buffered_endpoint_receive(server, 100);
        TEST_ASSERT_NOT_NULL(request);
        eros_endpoint_send_data_with_seq(server, request->source, request->sequence_number,
                                         (uint8_t *)"a", 1, 0);
        eros_package_delete(request);
    }

    // Both answers to the pool, each to its own realm; none to the other gateway endpoint
    for (int i = 0; i < 2; i++) {
        eros_package_t *answer = eros_buffered_gateway_endpoint_receive(pool, 100);
        TEST_ASSERT_NOT_NULL(answer);
        TEST_ASSERT_EQUAL(EROS_PACKAGE_TYPE_ID, answer->type);
        TEST_ASSERT_EQUAL(POOL_FIRST + 1 + i, answer->target.destination.realm_id);
        // The header on the wire: to 1@realm, from the server, the request's sequence
        const uint8_t *header = answer->data - sizeof(eros_header_t);
        TEST_ASSERT_EQUAL_HEX8(1 << 4 | (POOL_FIRST + 1 + i), header[0]);
        TEST_ASSERT_EQUAL_HEX8(SERVER_ID << 4 | ROUTER_REALM, header[1]);
        TEST_ASSERT_EQUAL(5, header[2] & 0x0f);
        eros_package_delete(answer);
    }
    TEST_ASSERT_EQUAL(0, eros_port_queue_count(other->endpoint.buffered_gateway_endpoint.queue));

    // An answer to a realm no endpoint serves goes nowhere
    eros_endpoint_send_data(server, (eros_id_t){.id = 1, .realm_id = 9}, (uint8_t *)"a", 1, 0);
    TEST_ASSERT_EQUAL(0, eros_port_queue_count(pool->endpoint.buffered_gateway_endpoint.queue));

    eros_endpoint_delete(other);
    eros_endpoint_delete(pool);
    eros_endpoint_delete(server);
    eros_router_delete(router);
}

void test_group_output_reaches_a_pool_once(void)
{
    eros_router_t *router = eros_router_new(ROUTER_REALM, ENDPOINTS_COUNT);
    eros_endpoint_t *console = eros_buffered_endpoint_new(1, router, QUEUE_SIZE);
    eros_endpoint_t *pool =
        eros_buffered_gateway_pool_new(11, router, QUEUE_SIZE, POOL_FIRST, POOL_COUNT);

    eros_router_register_endpoint(router, console);
    eros_router_register_endpoint(router, pool);
    eros_endpoint_subscribe_group(pool, 1);

    // Once in the queue: the transport sends it to each of its hosts
    eros_endpoint_publish_data(console, 1, (uint8_t *)"log", 3, 0);
    TEST_ASSERT_EQUAL(1, eros_port_queue_count(pool->endpoint.buffered_gateway_endpoint.queue));

    eros_package_t *output = eros_buffered_gateway_endpoint_receive(pool, 100);
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_EQUAL(EROS_PACKAGE_TYPE_GROUP, output->type);
    eros_package_delete(output);

    eros_endpoint_delete(pool);
    eros_endpoint_delete(console);
    eros_router_delete(router);
}

void test_unbuffered_gateway_gets_its_realms_packages(void)
{
    eros_router_t *router = eros_router_new(ROUTER_REALM, ENDPOINTS_COUNT);
    eros_endpoint_t *server = eros_buffered_endpoint_new(SERVER_ID, router, QUEUE_SIZE);
    eros_endpoint_t *gateway =
        eros_unbuffered_gateway_endpoint_new(13, router, 3, gateway_callback);

    eros_router_register_endpoint(router, server);
    eros_router_register_endpoint(router, gateway);

    // To its realm: handed over with the header
    eros_endpoint_send_data(server, (eros_id_t){.id = 4, .realm_id = 3}, (uint8_t *)"z", 1, 0);
    TEST_ASSERT_EQUAL(4, g_callback_size);
    TEST_ASSERT_EQUAL_HEX8(4 << 4 | 3, g_callback_frame[0]);

    // To another realm: not
    g_callback_size = 0;
    eros_endpoint_send_data(server, (eros_id_t){.id = 4, .realm_id = 2}, (uint8_t *)"z", 1, 0);
    TEST_ASSERT_EQUAL(0, g_callback_size);

    eros_endpoint_delete(gateway);
    eros_endpoint_delete(server);
    eros_router_delete(router);
}

void test_register_refuses_when_the_router_is_full(void)
{
    eros_router_t *router = eros_router_new(ROUTER_REALM, 1);
    eros_endpoint_t *first = eros_buffered_endpoint_new(1, router, QUEUE_SIZE);
    eros_endpoint_t *second = eros_buffered_endpoint_new(2, router, QUEUE_SIZE);

    TEST_ASSERT_EQUAL(0, eros_router_register_endpoint(router, first));
    TEST_ASSERT_EQUAL(-1, eros_router_register_endpoint(router, second));
    TEST_ASSERT_EQUAL(1, router->endpoint_count);

    eros_endpoint_delete(second);
    eros_endpoint_delete(first);
    eros_router_delete(router);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ingest_sets_the_hosts_source_realm);
    RUN_TEST(test_pool_refuses_a_realm_it_does_not_serve);
    RUN_TEST(test_answers_reach_only_the_host_that_asked);
    RUN_TEST(test_group_output_reaches_a_pool_once);
    RUN_TEST(test_unbuffered_gateway_gets_its_realms_packages);
    RUN_TEST(test_register_refuses_when_the_router_is_full);
    return UNITY_END();
}
