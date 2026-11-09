#include <stdint.h>
#include <stdlib.h>

#ifndef EROS_PACKAGE_H
#define EROS_PACKAGE_H

typedef uint8_t eros_realm_id_t;
typedef uint8_t eros_id_type_t;
typedef uint8_t eros_group_t;

typedef struct
{
  uint8_t target_realm : 4;
  uint8_t target_id : 4;
  uint8_t source_realm : 4;
  uint8_t source_id : 4;
  uint8_t sequence_number : 4; //Sequence number for this packet, can be used in the answer
  uint8_t reserved : 4;        // 0 on the wire
} eros_header_t;

typedef struct
{
  eros_id_type_t id;
  eros_realm_id_t realm_id;
} eros_id_t;

typedef enum
{
  EROS_PACKAGE_TYPE_GROUP,
  EROS_PACKAGE_TYPE_ID
} eros_package_type_t;

typedef struct
{
  eros_package_type_t type;
  uint8_t sequence_number;
  eros_id_t source;

  union
  {
    eros_id_t destination;
    eros_group_t group;
  } target;

  uint8_t *data;
  size_t size;
  uint8_t reference_count;
} eros_package_t;

eros_package_t *eros_package_new(uint8_t *data, size_t size);
void eros_package_delete(eros_package_t *package);
void eros_package_decrease_reference(eros_package_t *package);
void eros_package_increase_reference(eros_package_t *package);
eros_package_t *eros_headered_package_new(uint8_t *data, size_t size);
void eros_package_encode_header(eros_package_t *package);

/**
 * @brief The package as a frame on the wire: its header and its data, one buffer of *size bytes.
 * The header is the one last written into it (eros_package_encode_header();
 * eros_buffered_gateway_endpoint_receive() writes it).
 */
uint8_t *eros_package_frame(eros_package_t *package, size_t *size);

#endif // EROS_PACKAGE_H