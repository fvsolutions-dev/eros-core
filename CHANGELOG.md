# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
