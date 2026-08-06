
#include "eros.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Created a new eros package
 *
 * @param data Data to be copied into the package
 * @param size Size of the data
 * @return eros_package_t* Pointer to the new package, this package must be
 * freed with eros_package_delete
 */

eros_package_t *eros_package_new(uint8_t *data, size_t size)
{

  eros_package_t *package = malloc(sizeof(eros_package_t));
  if (package == NULL)
  {
    return NULL;
  }

  // Allocate buffer, but leave space for the header at the start
  uint8_t *data_buffer = malloc(size + sizeof(eros_header_t));
  if (data_buffer == NULL)
  {
    free(package);   // was free(data_buffer) — freed NULL and leaked the package
    return NULL;
  }

  memcpy(data_buffer + sizeof(eros_header_t), data, size);
  package->data = data_buffer + sizeof(eros_header_t);
  package->size = size;
  package->reference_count = 1;
  package->sequence_number = 0;

  return package;
}

eros_package_t *eros_headered_package_new(uint8_t *data_with_header,
                                          size_t size)
{

  if (size < sizeof(eros_header_t))
  {
    return NULL;
  }

  eros_package_t *package = eros_package_new(
      data_with_header + sizeof(eros_header_t), size - sizeof(eros_header_t));
  if (package == NULL)
  {
    return NULL;
  }
  package->type = EROS_PACKAGE_TYPE_ID;

  // Decode the header
  eros_header_t *header = (eros_header_t *)data_with_header;
  package->target.destination.id = header->target_id;
  package->target.destination.realm_id = header->target_realm;
  package->source.id = header->source_id;
  package->source.realm_id = header->source_realm;
  package->sequence_number = header->sequence_number;

  return package;
}

void eros_package_encode_header(eros_package_t *package)
{
  // Populate the header
  eros_header_t *header =
      (eros_header_t *)(package->data - sizeof(eros_header_t));
  header->target_id = package->target.destination.id;
  header->target_realm = package->target.destination.realm_id;
  header->source_id = package->source.id;
  header->source_realm = package->source.realm_id;
  header->sequence_number = package->sequence_number;
}

/* The reference count is shared across every subscriber queue a package is
 * fanned out to, and those subscribers release from different tasks on
 * different cores (CDC worker, HID worker, disk-log worker...). All counter
 * ops are atomic and eros_package_delete decides ownership from the value
 * RETURNED by its own decrement — the old decrement-then-reread pattern let
 * two concurrent releases both observe zero and double-free the package
 * (tlsf assert "block already marked as free"). */
void eros_package_increase_reference(eros_package_t *package)
{
  __atomic_add_fetch(&package->reference_count, 1, __ATOMIC_RELAXED);
}

void eros_package_decrease_reference(eros_package_t *package)
{
  __atomic_fetch_sub(&package->reference_count, 1, __ATOMIC_ACQ_REL);
}

void eros_package_delete(eros_package_t *package)
{
  uint8_t prev = __atomic_fetch_sub(&package->reference_count, 1, __ATOMIC_ACQ_REL);
  if (prev == 1)
  {
    free(package->data - sizeof(eros_header_t));
    free(package);
  }
}
