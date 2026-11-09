

#include "eros.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void eros_print_packag_debug(const char *tag, eros_package_t *package)
{
  #if 0
  printf("%s: src(realm: %d, id: %d) -> dest(realm: %d, id: %d) "
         "seq: %d size: %d\n",
         tag, package->source.realm_id, package->source.id,
         package->target.destination.realm_id, package->target.destination.id,
         package->sequence_number, package->size);
  #endif
}

eros_endpoint_t *eros_buffered_endpoint_new(int id, eros_router_t *router,
                                            int queue_size)
{
  assert(router);

  eros_port_queue_t *queue = eros_port_queue_create(queue_size, sizeof(eros_package_t *));

  if (queue == NULL)
  {
    return NULL;
  }

  eros_endpoint_t endpoint = {
      .id =
          {
              .id = id,
              .realm_id = router->realm_id,
          },
      .type = EROS_ENDPOINT_BUFFERED,
      .endpoint.buffered_endpoint =
          {
              .queue = queue,
          },
      .router = router,
  };

  eros_endpoint_t *endpoint_ptr = malloc(sizeof(eros_endpoint_t));

  if (endpoint_ptr == NULL)
  {
    eros_port_queue_destroy(queue);
    return NULL;
  }

  memcpy(endpoint_ptr, &endpoint, sizeof(eros_endpoint_t));
  return endpoint_ptr;
}

eros_endpoint_t *
eros_unbuffered_endpoint_new(int id, eros_router_t *router,
                             eros_package_callback_t callback)
{
  assert(router);

  eros_endpoint_t endpoint = {
      .id =
          {
              .id = id,
              .realm_id = router->realm_id,
          },
      .type = EROS_ENDPOINT_UNBUFFERED,
      .endpoint.unbuffered_endpoint =
          {
              .callback = callback,
          },
      .router = router,
  };

  eros_endpoint_t *endpoint_ptr = malloc(sizeof(eros_endpoint_t));

  if (endpoint_ptr == NULL)
  {
    return NULL;
  }

  memcpy(endpoint_ptr, &endpoint, sizeof(eros_endpoint_t));
  return endpoint_ptr;
}

eros_endpoint_t *
eros_buffered_gateway_endpoint_new(int id, eros_router_t *router,
                                   int queue_size,
                                   eros_realm_id_t remote_realm_id)
{
  assert(router);

  eros_port_queue_t *queue = eros_port_queue_create(queue_size, sizeof(eros_package_t *));

  if (queue == NULL)
  {
    return NULL;
  }
  // TODO: remove double remote realm id

  eros_endpoint_t endpoint = {
      .id =
          {
              .id = id,
              .realm_id = remote_realm_id,
          },
      .type = EROS_GATEWAY_BUFFERED,
      .endpoint.buffered_gateway_endpoint =
          {
              .queue = queue,
              .remote_realm_id = remote_realm_id,
              .realms = (uint16_t)(1u << remote_realm_id),
              .gateway_mode = EROS_GATEWAY_MODE_PROMISCUOUS,
          },
      .router = router,
  };

  eros_endpoint_t *endpoint_ptr = malloc(sizeof(eros_endpoint_t));

  if (endpoint_ptr == NULL)
  {
    eros_port_queue_destroy(queue);
    return NULL;
  }

  memcpy(endpoint_ptr, &endpoint, sizeof(eros_endpoint_t));
  return endpoint_ptr;
}

eros_endpoint_t *eros_buffered_gateway_pool_new(int id, eros_router_t *router, int queue_size,
                                                eros_realm_id_t first_realm, int realm_count)
{
  assert(realm_count > 0 && first_realm + realm_count <= 16);

  eros_endpoint_t *endpoint =
      eros_buffered_gateway_endpoint_new(id, router, queue_size, first_realm);

  if (endpoint == NULL)
  {
    return NULL;
  }
  // Every realm of the range, a bit each
  endpoint->endpoint.buffered_gateway_endpoint.realms =
      (uint16_t)(((1u << realm_count) - 1) << first_realm);
  return endpoint;
}

eros_endpoint_t *
eros_unbuffered_gateway_endpoint_new(int id, eros_router_t *router,
                                     eros_realm_id_t remote_realm_id,
                                     eros_data_callback_t callback)
{
  assert(router);

  eros_endpoint_t endpoint = {
      .id =
          {
              .id = id,
              .realm_id = remote_realm_id,
          },
      .type = EROS_UNBUFFERED_GATEWAY,
      .endpoint.unbuffered_gateway_endpoint =
          {
              .callback = callback,
              .remote_realm_id = remote_realm_id,
              .realms = (uint16_t)(1u << remote_realm_id),
              .gateway_mode = EROS_GATEWAY_MODE_PROMISCUOUS,
          },
      .router = router,
  };

  eros_endpoint_t *endpoint_ptr = malloc(sizeof(eros_endpoint_t));

  if (endpoint_ptr == NULL)
  {
    return NULL;
  }

  memcpy(endpoint_ptr, &endpoint, sizeof(eros_endpoint_t));
  return endpoint_ptr;
}

void eros_gateway_set_fixed_id_mode(eros_endpoint_t *endpoint,
                                    eros_id_t target)
{
  assert(endpoint);
  if (endpoint->type == EROS_UNBUFFERED_GATEWAY)
  {
    endpoint->endpoint.unbuffered_gateway_endpoint.gateway_mode =
        EROS_GATEWAY_MODE_FIXED_ID;
    endpoint->endpoint.unbuffered_gateway_endpoint.fixed_target = target;
  }
  else
  {
    endpoint->endpoint.buffered_gateway_endpoint.gateway_mode =
        EROS_GATEWAY_MODE_FIXED_ID;
    endpoint->endpoint.buffered_gateway_endpoint.fixed_target = target;
  }
}

void eros_gateway_set_realms(eros_endpoint_t *endpoint, uint16_t realms)
{
  assert(endpoint);
  // Never the router's own realm (eros_endpoint.h)
  realms &= (uint16_t)~(1u << endpoint->router->realm_id);
  if (endpoint->type == EROS_UNBUFFERED_GATEWAY)
  {
    endpoint->endpoint.unbuffered_gateway_endpoint.realms = realms;
  }
  else
  {
    endpoint->endpoint.buffered_gateway_endpoint.realms = realms;
  }
}

void eros_gateway_set_fixed_target_group_mode(eros_endpoint_t *endpoint,
                                              eros_group_t group)
{
  assert(endpoint);
  if (endpoint->type == EROS_UNBUFFERED_GATEWAY)
  {
    endpoint->endpoint.unbuffered_gateway_endpoint.gateway_mode =
        EROS_GATEWAY_MODE_FIXED_GROUP;
    endpoint->endpoint.unbuffered_gateway_endpoint.fixed_group = group;
  }
  else
  {
    endpoint->endpoint.buffered_gateway_endpoint.gateway_mode =
        EROS_GATEWAY_MODE_FIXED_GROUP;
    endpoint->endpoint.buffered_gateway_endpoint.fixed_group = group;
  }
}

void eros_endpoint_delete(eros_endpoint_t *endpoint)
{
  if (endpoint == NULL)
  {
    return;
  }

  switch (endpoint->type)
  {
  case EROS_ENDPOINT_BUFFERED:
    if (endpoint->endpoint.buffered_endpoint.queue != NULL)
    {
      eros_port_queue_destroy(endpoint->endpoint.buffered_endpoint.queue);
    }
    break;
  case EROS_GATEWAY_BUFFERED:
    if (endpoint->endpoint.buffered_gateway_endpoint.queue != NULL)
    {
      eros_port_queue_destroy(endpoint->endpoint.buffered_gateway_endpoint.queue);
    }
    break;
  default:
    break;
  }

  free(endpoint);
}

eros_gateway_mode_enum_t
eros_endpoint_gateway_get_mode(eros_endpoint_t *endpoint)
{
  assert(endpoint);
  if (endpoint->type == EROS_UNBUFFERED_GATEWAY)
  {
    return endpoint->endpoint.unbuffered_gateway_endpoint.gateway_mode;
  }
  else
  {
    return endpoint->endpoint.buffered_gateway_endpoint.gateway_mode;
  }
}

void eros_endpoint_subscribe_group(eros_endpoint_t *endpoint,
                                   eros_group_t group)
{
  endpoint->subscribed_group_bitmap |= (1 << group);
}

int eros_endpoint_send_data(eros_endpoint_t *endpoint, eros_id_t destination,
                            uint8_t *data, size_t size, uint32_t timeout_ms)
{
  eros_package_t *package = eros_package_new(data, size);
  if (package == NULL)
  {
    return -1;
  }

  package->source = endpoint->id;
  package->type = EROS_PACKAGE_TYPE_ID;
  package->target.destination = destination;

  eros_print_packag_debug("TX", package);

  // Route the package to the router
  eros_router_route(endpoint->router, package, timeout_ms);

  // Free the package (of not sent it will be freed)
  eros_package_delete(package);
  return 0;
}
int eros_endpoint_send_data_with_seq(eros_endpoint_t *endpoint, eros_id_t destination, uint8_t sequence,
                                     uint8_t *data, size_t size, uint32_t timeout_ms)
{
  eros_package_t *package = eros_package_new(data, size);
  if (package == NULL)
  {
    return -1;
  }

  package->source = endpoint->id;
  package->type = EROS_PACKAGE_TYPE_ID;
  package->target.destination = destination;
  package->sequence_number = sequence;

  eros_print_packag_debug("TX", package);

  // Route the package to the router
  eros_router_route(endpoint->router, package, timeout_ms);

  // Free the package (of not sent it will be freed)
  eros_package_delete(package);
  return 0;
}

int eros_endpoint_publish_data(eros_endpoint_t *endpoint, eros_group_t group,
                               uint8_t *data, size_t size, uint32_t timeout_ms)
{
  eros_package_t *package = eros_package_new(data, size);
  if (package == NULL)
  {
    return -1;
  }

  package->source = endpoint->id;
  package->type = EROS_PACKAGE_TYPE_GROUP;
  /* target is a union and group only overlays destination.id; assign through
     destination so realm_id is not left as uninitialized malloc memory — a
     promiscuous gateway encodes that nibble onto the wire. */
  package->target.destination = (eros_id_t){.id = group, .realm_id = 0};

  eros_router_route(endpoint->router, package, timeout_ms);
  eros_package_delete(package);
  return 0;
}

/** @brief A frame with its header into the router, its source realm set to the host's. */
static int publish_headered_data(eros_endpoint_t *endpoint, eros_realm_id_t realm, uint8_t *data,
                                 size_t size, uint32_t timeout_ms)
{
  eros_package_t *package = eros_headered_package_new(data, size);

  if (package == NULL)
  {
    return -1;
  }
  // The realm the host is in, not the one it wrote: answers find their way back to it, and a host
  // cannot speak for another
  package->source.realm_id = realm;

  eros_print_packag_debug("RX", package);

  eros_router_route(endpoint->router, package, timeout_ms);
  eros_package_delete(package);
  return 0;
}

int eros_endpoint_gateway_ingest(eros_endpoint_t *endpoint, uint8_t *data,
                                 size_t size, uint32_t timeout_ms)
{
  bool unbuffered = endpoint->type == EROS_UNBUFFERED_GATEWAY;
  uint16_t realms = unbuffered ? endpoint->endpoint.unbuffered_gateway_endpoint.realms
                               : endpoint->endpoint.buffered_gateway_endpoint.realms;
  // The endpoint's own realm, for a frame from a realm that is not behind it (a host's)
  eros_realm_id_t realm = unbuffered ? endpoint->endpoint.unbuffered_gateway_endpoint.remote_realm_id
                                     : endpoint->endpoint.buffered_gateway_endpoint.remote_realm_id;

  // A frame from a realm behind the endpoint (through another router) keeps it
  if (size >= sizeof(eros_header_t))
  {
    eros_realm_id_t source = ((const eros_header_t *)data)->source_realm;

    if (realms & (1u << source))
    {
      realm = source;
    }
  }
  return eros_endpoint_gateway_ingest_from(endpoint, realm, data, size, timeout_ms);
}

int eros_endpoint_gateway_ingest_from(eros_endpoint_t *endpoint, eros_realm_id_t realm,
                                      uint8_t *data, size_t size, uint32_t timeout_ms)
{
  eros_gateway_mode_enum_t mode = eros_endpoint_gateway_get_mode(endpoint);
  uint16_t realms = endpoint->type == EROS_UNBUFFERED_GATEWAY
                        ? endpoint->endpoint.unbuffered_gateway_endpoint.realms
                        : endpoint->endpoint.buffered_gateway_endpoint.realms;

  // Only from a host connection of this endpoint
  if (realm >= 16 || !(realms & (1u << realm)))
  {
    return -1;
  }
  if (mode == EROS_GATEWAY_MODE_PROMISCUOUS)
  {
    return publish_headered_data(endpoint, realm, data, size, timeout_ms);
  }
  else if (mode == EROS_GATEWAY_MODE_FIXED_GROUP)
  {
    return eros_endpoint_publish_data(
        endpoint, endpoint->endpoint.unbuffered_gateway_endpoint.fixed_group,
        data, size, timeout_ms);
  }
  else
  {
    return eros_endpoint_send_data(
        endpoint, endpoint->endpoint.unbuffered_gateway_endpoint.fixed_target,
        data, size, timeout_ms);
  }
}
eros_package_t *eros_buffered_endpoint_receive(eros_endpoint_t *endpoint,
                                               uint32_t timeout_ms)
{
  assert(endpoint);
  assert(endpoint->type == EROS_ENDPOINT_BUFFERED);
  eros_package_t *package = NULL;
  eros_port_queue_recv(endpoint->endpoint.buffered_endpoint.queue, &package, timeout_ms);
  return package;
}

eros_package_t *
eros_buffered_gateway_endpoint_receive(eros_endpoint_t *endpoint,
                                       uint32_t timeout_ms)
{
  assert(endpoint);
  assert(endpoint->type == EROS_GATEWAY_BUFFERED);

  eros_package_t *package = NULL;
  eros_port_queue_recv(endpoint->endpoint.buffered_gateway_endpoint.queue, &package,
                       timeout_ms);

  // Add header (only if we actually got a package — receive may have
  // timed out with no data)
  if (package != NULL) {
    eros_package_encode_header(package);
  }

  return package;
}

int eros_endpoint_send(eros_endpoint_t *endpoint, eros_package_t *package,
                       uint32_t timeout_ms)
{
  assert(endpoint);
  assert(package);

  // increment the reference count
  switch (endpoint->type)
  {
  case EROS_ENDPOINT_BUFFERED:
    eros_package_increase_reference(package);

    if (!eros_port_queue_send(endpoint->endpoint.buffered_endpoint.queue, &package,
                              timeout_ms))
    {
      eros_package_decrease_reference(package);
      // Publishers on several tasks at once: counted atomically
      __atomic_fetch_add(&endpoint->dropped, 1, __ATOMIC_RELAXED);
      return -1;
    }
    return 0;

  case EROS_GATEWAY_BUFFERED:
    eros_package_increase_reference(package);

    if (!eros_port_queue_send(endpoint->endpoint.buffered_gateway_endpoint.queue,
                              &package, timeout_ms))
    {
      eros_package_decrease_reference(package);
      // Publishers on several tasks at once: counted atomically
      __atomic_fetch_add(&endpoint->dropped, 1, __ATOMIC_RELAXED);
      return -1;
    }

    return 0;

  case EROS_ENDPOINT_UNBUFFERED:
    if (endpoint->endpoint.unbuffered_endpoint.callback)
    {
      endpoint->endpoint.unbuffered_endpoint.callback(endpoint, package);
      return 0;
    }
    return -1;

  case EROS_UNBUFFERED_GATEWAY:
    if (endpoint->endpoint.unbuffered_gateway_endpoint.callback)
    {

      if (endpoint->endpoint.unbuffered_gateway_endpoint.gateway_mode ==
          EROS_GATEWAY_MODE_PROMISCUOUS)
      {
        // Add header data
        eros_package_encode_header(package);
        endpoint->endpoint.unbuffered_gateway_endpoint.callback(
            endpoint, package->data - sizeof(eros_header_t),
            package->size + sizeof(eros_header_t));
      }
      else
      {

        // Do not add header data
        endpoint->endpoint.unbuffered_gateway_endpoint.callback(
            endpoint, package->data, package->size);
      }

      return 0;
    }
    return -1;

  case EROS_ENDPOINT_WORKER:
    if (endpoint->endpoint.worker_endpoint.worker_callback)
    {
      endpoint->endpoint.worker_endpoint.worker_callback(endpoint, package);
      return 0;
    }
    return -1;

  default:
    return -1;
  }
}

void eros_endpoint_set_callback(eros_endpoint_t *endpoint,
                                eros_package_callback_t callback)
{
  assert(endpoint);
  switch (endpoint->type)
  {
  case EROS_ENDPOINT_UNBUFFERED:
    endpoint->endpoint.unbuffered_endpoint.callback = callback;
    break;
  case EROS_UNBUFFERED_GATEWAY:
    endpoint->endpoint.unbuffered_gateway_endpoint.callback =
        (eros_data_callback_t)(void *)callback;
    break;
  case EROS_ENDPOINT_WORKER:
    endpoint->endpoint.worker_endpoint.callback = callback;
    break;
  default:
    break;
  }
}
