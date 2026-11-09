
#include "eros.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

eros_router_t *eros_router_new(int realm_id, int endpoints_count)
{
    eros_router_t router = {
        .realm_id = realm_id,
        .endpoints = malloc(endpoints_count * sizeof(eros_endpoint_t *)),
        .endpoint_count = 0,
        .endpoint_limit = endpoints_count,
    };

    if (router.endpoints == NULL)
    {
        return NULL;
    }

    eros_router_t *router_ptr = malloc(sizeof(eros_router_t));

    if (router_ptr == NULL)
    {
        free(router.endpoints);
        return NULL;
    }

    memcpy(router_ptr, &router, sizeof(eros_router_t));
    return router_ptr;
}

void eros_router_delete(eros_router_t *router)
{
    if (router == NULL)
    {
        return;
    }

    if (router->endpoints != NULL)
    {
        free(router->endpoints);
        router->endpoints = NULL;
    }

    free(router);
}

int eros_router_register_endpoint(eros_router_t *router, eros_endpoint_t *endpoint)
{
    // Full: the table is not written past
    if (router->endpoint_count >= router->endpoint_limit)
    {
        return -1;
    }
    router->endpoints[router->endpoint_count] = endpoint;
    router->endpoint_count++;
    return 0;
}

/** @brief The realms behind a gateway endpoint, a bit each; none for any other endpoint. */
static uint16_t gateway_realms(const eros_endpoint_t *endpoint)
{
    switch (endpoint->type)
    {
    // A gateway endpoint: its realms
    case EROS_GATEWAY_BUFFERED:
        return endpoint->endpoint.buffered_gateway_endpoint.realms;
    case EROS_UNBUFFERED_GATEWAY:
        return endpoint->endpoint.unbuffered_gateway_endpoint.realms;
    // Any other: none
    default:
        return 0;
    }
}

void eros_router_route(eros_router_t *router, eros_package_t *package, uint32_t timeout_ms)
{
    for (int i = 0; i < router->endpoint_count; i++)
    {
        bool send_to_endpoint = false;

        if (package->type == EROS_PACKAGE_TYPE_ID)
        {
            if ((router->endpoints[i]->id.id == package->target.destination.id) && (router->endpoints[i]->id.realm_id == package->target.destination.realm_id))
            {
                send_to_endpoint = true;
            }
            // A gateway endpoint with the destination's realm behind it
            else if (package->target.destination.realm_id < 16 &&
                     (gateway_realms(router->endpoints[i]) & (1u << package->target.destination.realm_id)))
            {
                send_to_endpoint = true;
            }
        }
        else if (package->type == EROS_PACKAGE_TYPE_GROUP)
        {
            if (router->endpoints[i]->subscribed_group_bitmap & (1 << package->target.group))
            {
                send_to_endpoint = true;
            }
        }
        

        if (send_to_endpoint)
        {
            if (eros_endpoint_send(router->endpoints[i], package, timeout_ms))
            {
                // Do not print here because it will cause a infinite recursion
                // printf("Failed to send package to endpoint %d\n", router->endpoints[i]->id.id);
            }
        }
    }
}
