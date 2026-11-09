/* Gateway endpoints: a host's source realm is set where its frame enters, a pool endpoint serves
 * a range of realms (one per host connection), and answers reach only the host that asked, also
 * through a second router.
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

// Two routers linked by a pair of unbuffered gateway endpoints: what one hands over, the other
// takes in (gateway_test's two-hop test)
static eros_endpoint_t *g_gateway_to_imu;
static eros_endpoint_t *g_imu_uplink;

/** @brief The gateway's link to the IMU: the frame into the IMU's router. */
static void to_imu(eros_endpoint_t *endpoint, uint8_t *data, size_t size)
{
    (void)endpoint;
    eros_endpoint_gateway_ingest(g_imu_uplink, data, size, 0);
}

/** @brief The IMU's uplink: the frame into the gateway's router. */
static void to_gateway(eros_endpoint_t *endpoint, uint8_t *data, size_t size)
{
    (void)endpoint;
    eros_endpoint_gateway_ingest(g_gateway_to_imu, data, size, 0);
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
        // The sequence, and the reserved bits 0
        TEST_ASSERT_EQUAL_HEX8(5, header[2]);
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

void test_package_frame_is_the_header_and_the_data(void)
{
    eros_router_t *router = eros_router_new(ROUTER_REALM, ENDPOINTS_COUNT);
    eros_endpoint_t *server = eros_buffered_endpoint_new(SERVER_ID, router, QUEUE_SIZE);
    eros_endpoint_t *gateway = eros_buffered_gateway_endpoint_new(14, router, QUEUE_SIZE, 5);

    eros_router_register_endpoint(router, server);
    eros_router_register_endpoint(router, gateway);
    eros_endpoint_send_data_with_seq(server, (eros_id_t){.id = 1, .realm_id = 5}, 7,
                                     (uint8_t *)"abc", 3, 0);

    // As it goes on the wire: to 1@5, from the server, sequence 7, then the data
    eros_package_t *package = eros_buffered_gateway_endpoint_receive(gateway, 100);
    TEST_ASSERT_NOT_NULL(package);
    size_t size;
    const uint8_t *frame = eros_package_frame(package, &size);
    const uint8_t expected[] = {1 << 4 | 5, SERVER_ID << 4 | ROUTER_REALM, 7, 'a', 'b', 'c'};
    TEST_ASSERT_EQUAL(sizeof(expected), size);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, frame, sizeof(expected));
    eros_package_delete(package);

    eros_endpoint_delete(gateway);
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

void test_an_answer_finds_its_host_over_two_hops(void)
{
    // The gateway (realm 0): a pool of hosts and the link to the IMU, realm 14
    eros_router_t *gateway = eros_router_new(ROUTER_REALM, ENDPOINTS_COUNT);
    eros_endpoint_t *pool =
        eros_buffered_gateway_pool_new(11, gateway, QUEUE_SIZE, POOL_FIRST, POOL_COUNT);
    g_gateway_to_imu = eros_unbuffered_gateway_endpoint_new(10, gateway, 14, to_imu);
    eros_router_register_endpoint(gateway, pool);
    eros_router_register_endpoint(gateway, g_gateway_to_imu);

    // The IMU (realm 14): its management endpoint and the uplink, its default route
    eros_router_t *imu = eros_router_new(14, ENDPOINTS_COUNT);
    eros_endpoint_t *management = eros_buffered_endpoint_new(SERVER_ID, imu, QUEUE_SIZE);
    g_imu_uplink = eros_unbuffered_gateway_endpoint_new(10, imu, ROUTER_REALM, to_gateway);
    eros_gateway_set_realms(g_imu_uplink, 0xffff);
    eros_router_register_endpoint(imu, management);
    eros_router_register_endpoint(imu, g_imu_uplink);

    // A host in the pool's second realm asks the IMU's management
    uint8_t frame[16];
    size_t size = make_frame(frame, SERVER_ID, 14, 1, 0, 5, "get");
    TEST_ASSERT_EQUAL(0, eros_endpoint_gateway_ingest_from(pool, POOL_FIRST + 1, frame, size, 0));

    // At the IMU: from the host's realm, kept over the hop
    eros_package_t *request = eros_buffered_endpoint_receive(management, 100);
    TEST_ASSERT_NOT_NULL(request);
    TEST_ASSERT_EQUAL(1, request->source.id);
    TEST_ASSERT_EQUAL(POOL_FIRST + 1, request->source.realm_id);
    eros_endpoint_send_data_with_seq(management, request->source, request->sequence_number,
                                     (uint8_t *)"42", 2, 0);
    eros_package_delete(request);

    // Back at the gateway: to that host's realm, from the IMU's management
    eros_package_t *answer = eros_buffered_gateway_endpoint_receive(pool, 100);
    TEST_ASSERT_NOT_NULL(answer);
    size_t answer_size;
    const uint8_t *answer_frame = eros_package_frame(answer, &answer_size);
    const uint8_t expected[] = {1 << 4 | (POOL_FIRST + 1), SERVER_ID << 4 | 14, 5, '4', '2'};
    TEST_ASSERT_EQUAL(sizeof(expected), answer_size);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, answer_frame, sizeof(expected));
    eros_package_delete(answer);

    // A package for a missing endpoint in the IMU's own realm stays there: no bounce
    eros_endpoint_send_data(management, (eros_id_t){.id = 7, .realm_id = 14}, (uint8_t *)"x", 1, 0);
    TEST_ASSERT_EQUAL(0, eros_port_queue_count(pool->endpoint.buffered_gateway_endpoint.queue));

    eros_endpoint_delete(g_imu_uplink);
    eros_endpoint_delete(management);
    eros_router_delete(imu);
    eros_endpoint_delete(g_gateway_to_imu);
    eros_endpoint_delete(pool);
    eros_router_delete(gateway);
}

void test_a_host_cannot_claim_a_realm_behind_another_link(void)
{
    eros_router_t *router = eros_router_new(ROUTER_REALM, ENDPOINTS_COUNT);
    eros_endpoint_t *server = eros_buffered_endpoint_new(SERVER_ID, router, QUEUE_SIZE);
    eros_endpoint_t *host = eros_buffered_gateway_endpoint_new(14, router, QUEUE_SIZE, 4);
    eros_router_register_endpoint(router, server);
    eros_router_register_endpoint(router, host);

    // The host on realm 4 writes realm 6, another link's: it is in realm 4
    uint8_t frame[16];
    size_t size = make_frame(frame, SERVER_ID, ROUTER_REALM, 1, 6, 0, "x");
    TEST_ASSERT_EQUAL(0, eros_endpoint_gateway_ingest(host, frame, size, 0));
    eros_package_t *package = eros_buffered_endpoint_receive(server, 100);
    TEST_ASSERT_NOT_NULL(package);
    TEST_ASSERT_EQUAL(4, package->source.realm_id);
    eros_package_delete(package);

    eros_endpoint_delete(host);
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
    RUN_TEST(test_package_frame_is_the_header_and_the_data);
    RUN_TEST(test_group_output_reaches_a_pool_once);
    RUN_TEST(test_unbuffered_gateway_gets_its_realms_packages);
    RUN_TEST(test_an_answer_finds_its_host_over_two_hops);
    RUN_TEST(test_a_host_cannot_claim_a_realm_behind_another_link);
    RUN_TEST(test_register_refuses_when_the_router_is_full);
    return UNITY_END();
}
