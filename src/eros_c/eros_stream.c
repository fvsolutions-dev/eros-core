// Eros frames on a byte stream (eros_stream.h). Compiled by the project that uses it, not by the
// eros library, so eros does not depend on burst-link

#include "eros_stream.h"

#include <burst/burst_encoder.h>

static int on_frame(const uint8_t *data, size_t size, void *context);

void eros_stream_init(eros_stream_t *stream, eros_endpoint_t *endpoint, int realm)
{
  stream->endpoint = endpoint;
  stream->realm = realm;
  // A fresh decoder: nothing left over from the last connection
  burst_managed_decoder_init(&stream->decoder, stream->decoded, sizeof(stream->decoded),
                             on_frame, stream);
}

void eros_stream_receive(eros_stream_t *stream, const uint8_t *bytes, size_t size)
{
  burst_managed_decoder_handle_data(&stream->decoder, bytes, size);
}

/** @brief A whole frame off the stream, its CRC checked: into the router. */
static int on_frame(const uint8_t *data, size_t size, void *context)
{
  eros_stream_t *stream = context;

  // From a realm of a pool, or as the endpoint takes it (eros drops a frame shorter than a header)
  if (stream->realm == EROS_STREAM_OWN_REALM)
  {
    return eros_endpoint_gateway_ingest(stream->endpoint, (uint8_t *)data, size, 0);
  }
  return eros_endpoint_gateway_ingest_from(stream->endpoint, (eros_realm_id_t)stream->realm,
                                           (uint8_t *)data, size, 0);
}

size_t eros_stream_encode(eros_package_t *package, uint8_t *buffer, size_t size)
{
  burst_encoder_t encoder;
  size_t frame_size;
  const uint8_t *frame = eros_package_frame(package, &frame_size);

  burst_encoder_init(&encoder, buffer, size);
  if (burst_encoder_add_packet(&encoder, frame, frame_size) != BURST_PACKET_READY)
  {
    return 0;
  }
  // The framed bytes start at the buffer
  return burst_encoder_flush(&encoder).size;
}
