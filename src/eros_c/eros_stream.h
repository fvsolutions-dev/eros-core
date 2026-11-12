// Eros frames on a byte stream (a UART, a USB CDC port, a BLE characteristic): each frame, its
// header and its data, burst-link framed (COBS and a CRC-16, burst-link-protocol). Bytes from the
// stream go into a gateway endpoint as whole frames; a package goes out as framed bytes.
//
// Not part of the eros library's own sources: a project that uses it compiles eros_stream.c and
// builds burst-link-protocol as well (its src/ on the include path). Eros itself does not need
// burst-link.

#ifndef EROS_STREAM_H
#define EROS_STREAM_H

#include <stddef.h>
#include <stdint.h>

#include <burst/burst_decoder.h>

#include "eros.h"

// The largest frame on the stream, its header included; a project may set its own, the same for
// every file that includes this one and for eros_stream.c
#ifndef EROS_STREAM_MAX_FRAME
#define EROS_STREAM_MAX_FRAME 512
#endif

// A frame burst-link framed, at worst: for a buffer eros_stream_encode() writes into
#define EROS_STREAM_MAX_ENCODED BURST_MAX_ENCODED(EROS_STREAM_MAX_FRAME)

// For eros_stream_init(): frames in with eros_endpoint_gateway_ingest(), the endpoint's own realm
// or one behind it, instead of a realm of a pool
#define EROS_STREAM_OWN_REALM (-1)

typedef struct
{
  eros_endpoint_t *endpoint;
  // The realm the stream's frames come from (a pool's), or EROS_STREAM_OWN_REALM
  int realm;
  burst_managed_decoder_t decoder;
  // A frame being put back together, and its CRC
  uint8_t decoded[EROS_STREAM_MAX_FRAME + CRC_SIZE];
} eros_stream_t;

/**
 * @brief A stream into a gateway endpoint, its frames from the given realm of a pool or
 * EROS_STREAM_OWN_REALM. Again for a new connection: nothing is left over from the last one.
 */
void eros_stream_init(eros_stream_t *stream, eros_endpoint_t *endpoint, int realm);

/** @brief Bytes as they came off the stream, in any pieces: each whole frame into the router. */
void eros_stream_receive(eros_stream_t *stream, const uint8_t *bytes, size_t size);

/**
 * @brief A package as framed bytes for the stream, written into buffer (EROS_STREAM_MAX_ENCODED
 * fits any frame). Returns their count, 0 when they do not fit. The package's header must be
 * written (eros_buffered_gateway_endpoint_receive() does).
 */
size_t eros_stream_encode(eros_package_t *package, uint8_t *buffer, size_t size);

#endif // EROS_STREAM_H
