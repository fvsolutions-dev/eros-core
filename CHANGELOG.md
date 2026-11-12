# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.1.0] - 2026-11-12

### Added
- Pool gateway endpoints (`eros_buffered_gateway_pool_new`): one endpoint serves a range of
  realms, one per host connection, and `eros_endpoint_gateway_ingest_from` takes a frame from the
  connection of a given realm.
- `eros_package_frame`: a package as its frame on the wire, header and data in one buffer.
- `eros_gateway_set_realms`: the realms behind a gateway endpoint, for a link to another router;
  a frame from one of them keeps its source realm, so answers find their way back over two hops.
- `eros_stream.h`: eros frames on a byte stream, burst-link framed. Not in the library's own
  sources: a project that uses it compiles `eros_stream.c` and builds burst-link-protocol too.

### Changed
- A gateway endpoint writes its host's realm into the source of each frame it takes in, in place
  of the realm the host wrote.
- The Python client delivers a received message to the endpoint with its target id, whatever the
  target realm.
- `eros_router_register_endpoint` returns -1 when the router is full, instead of writing past its
  table.

### Fixed
- `eros_package_new` no longer leaks the package when its data buffer cannot be allocated.
- The header's 4 reserved bits go out as 0, instead of whatever the package's buffer held.

## [2.0.0] - 2026-06-16

First release from `fvsolutions-dev/eros-core`

### Added
- 3-byte addressing header that multiplexes multiple logical endpoints over a
  single transport (UART, UDP, WebSocket, …).
- Unicast delivery addressed by `(realm, id)`.
- Group fan-out (pub/sub): publish to a group; every subscribed endpoint
  receives its own copy through the same router.
- C library (`src/eros_c/`) targeting FreeRTOS, ESP-IDF, and Zephyr.
- Async Python client (`src/eros/`) built on `asyncio`.
- MPL-2.0 license.

[2.0.0]: https://github.com/fvsolutions-dev/eros-core/releases/tag/v2.0.0
