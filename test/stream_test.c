/* Eros frames on a byte stream (eros_stream.h): bytes in any pieces become whole frames in the
 * router, from the stream's realm; a damaged frame is dropped and the next one still comes through;
 * a package goes out as framed bytes.
 */

#include <string.h>

#include "eros.h"
#include "eros_stream.h"
#include "unity.h"

#define ENDPOINTS_COUNT 8
#define QUEUE_SIZE      8
#define ROUTER_REALM    0
#define SERVER_ID       2

static eros_router_t   *g_router;
static eros_endpoint_t *g_server;
static eros_stream_t    g_stream;

void setUp(void)
{
    g_router = eros_router_new(ROUTER_REALM, ENDPOINTS_COUNT);
    g_server = eros_buffered_endpoint_new(SERVER_ID, g_router, QUEUE_SIZE);
    eros_router_register_endpoint(g_router, g_server);
}

void tearDown(void)
{
    eros_package_t *package;

    while ((package = eros_buffered_endpoint_receive(g_server, 0)) != NULL) {
        eros_package_delete(package);
    }
    eros_endpoint_delete(g_server);
    eros_router_delete(g_router);
}

/** @brief A host's frame to the server, from 1@9 with the given data, as framed bytes. */
static size_t host_bytes(uint8_t *bytes, size_t size, const char *data)
{
    uint8_t frame[32];
    size_t data_size = strlen(data);

    frame[0] = SERVER_ID << 4 | ROUTER_REALM;
    frame[1] = 1 << 4 | 9;
    frame[2] = 3;
    memcpy(frame + 3, data, data_size);

    // Through a package, as a transport would send it
    eros_package_t *package = eros_headered_package_new(frame, 3 + data_size);
    eros_package_encode_header(package);
    size_t encoded = eros_stream_encode(package, bytes, size);
    eros_package_delete(package);
    return encoded;
}

void test_bytes_in_pieces_become_a_frame_from_the_endpoints_realm(void)
{
    eros_endpoint_t *usb = eros_buffered_gateway_endpoint_new(15, g_router, QUEUE_SIZE, 4);
    uint8_t bytes[EROS_STREAM_MAX_ENCODED];
    size_t size = host_bytes(bytes, sizeof(bytes), "hello");

    eros_router_register_endpoint(g_router, usb);
    eros_stream_init(&g_stream, usb, EROS_STREAM_OWN_REALM);

    // A byte at a time, as a slow link hands them over
    TEST_ASSERT_GREATER_THAN(8, size);
    for (size_t i = 0; i < size; i++) {
        eros_stream_receive(&g_stream, bytes + i, 1);
    }

    // From the host's realm, 4, not the 9 it wrote
    eros_package_t *package = eros_buffered_endpoint_receive(g_server, 100);
    TEST_ASSERT_NOT_NULL(package);
    TEST_ASSERT_EQUAL(1, package->source.id);
    TEST_ASSERT_EQUAL(4, package->source.realm_id);
    TEST_ASSERT_EQUAL(3, package->sequence_number);
    TEST_ASSERT_EQUAL(5, package->size);
    TEST_ASSERT_EQUAL_MEMORY("hello", package->data, 5);
    eros_package_delete(package);
    eros_endpoint_delete(usb);
}

void test_a_stream_of_a_pool_comes_from_its_realm(void)
{
    eros_endpoint_t *pool = eros_buffered_gateway_pool_new(12, g_router, QUEUE_SIZE, 5, 3);
    uint8_t bytes[EROS_STREAM_MAX_ENCODED];
    size_t size = host_bytes(bytes, sizeof(bytes), "x");

    eros_router_register_endpoint(g_router, pool);
    eros_stream_init(&g_stream, pool, 6);
    eros_stream_receive(&g_stream, bytes, size);

    eros_package_t *package = eros_buffered_endpoint_receive(g_server, 100);
    TEST_ASSERT_NOT_NULL(package);
    TEST_ASSERT_EQUAL(6, package->source.realm_id);
    eros_package_delete(package);
    eros_endpoint_delete(pool);
}

void test_a_damaged_frame_is_dropped_and_the_next_comes_through(void)
{
    eros_endpoint_t *usb = eros_buffered_gateway_endpoint_new(15, g_router, QUEUE_SIZE, 4);
    uint8_t damaged[EROS_STREAM_MAX_ENCODED];
    uint8_t good[EROS_STREAM_MAX_ENCODED];
    size_t damaged_size = host_bytes(damaged, sizeof(damaged), "bad");
    size_t good_size = host_bytes(good, sizeof(good), "good");

    eros_router_register_endpoint(g_router, usb);
    eros_stream_init(&g_stream, usb, EROS_STREAM_OWN_REALM);

    // A bit flipped in the data: its CRC fails
    damaged[damaged_size / 2] ^= 0x01;
    eros_stream_receive(&g_stream, damaged, damaged_size);
    eros_stream_receive(&g_stream, good, good_size);

    eros_package_t *package = eros_buffered_endpoint_receive(g_server, 100);
    TEST_ASSERT_NOT_NULL(package);
    TEST_ASSERT_EQUAL(4, package->size);
    TEST_ASSERT_EQUAL_MEMORY("good", package->data, 4);
    eros_package_delete(package);
    TEST_ASSERT_NULL(eros_buffered_endpoint_receive(g_server, 0));
    eros_endpoint_delete(usb);
}

void test_encode_into_a_buffer_too_small_gives_0(void)
{
    uint8_t bytes[4];

    TEST_ASSERT_EQUAL(0, host_bytes(bytes, sizeof(bytes), "too long for four bytes"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_bytes_in_pieces_become_a_frame_from_the_endpoints_realm);
    RUN_TEST(test_a_stream_of_a_pool_comes_from_its_realm);
    RUN_TEST(test_a_damaged_frame_is_dropped_and_the_next_comes_through);
    RUN_TEST(test_encode_into_a_buffer_too_small_gives_0);
    return UNITY_END();
}
