# ASIO on Windows

ASIO (Audio Stream Input/Output) is Steinberg's low-latency Windows driver
API. It is the right target for this application on Windows: it is what every
serious interface ships a driver for, and it is the only way to address an
interface's individual output channels reliably.

## The SDK is not in this repository, and must not be

The Steinberg ASIO SDK **cannot be redistributed**. Using it requires agreeing
to Steinberg's licensing terms directly, via their developer site. That means:

- `third_party/asiosdk/` is gitignored. Do not commit it, ever.
- ASIO support is a **compile-time opt-in** (`-DBT_ENABLE_ASIO=ON`), off by
  default.
- CI builds without ASIO. The test suite does not need an audio device, which
  is the entire point of the headless engine design.
- Binary releases that include ASIO support require the signed agreement to be
  in place first.

## Building with ASIO locally

1. Obtain the ASIO SDK from Steinberg and accept their terms.
2. Unpack it to `third_party/asiosdk/`.
3. Configure with `-DBT_ENABLE_ASIO=ON`.

This wiring lands with the PortAudio device layer in Phase 2; until then the
engine is driven only by the offline renderer.

## Why not just use WASAPI?

WASAPI exclusive mode is genuinely good now and is the sensible default for
builds that cannot use ASIO - CI, contributors without the SDK, and anyone who
does not need per-channel routing. It is less predictable about addressing a
specific interface's individual output channels, which is exactly what a click
send to outputs 3/4 depends on. Plan to support both and let the machine's
`device.json` choose.

## One device only

ASIO exposes exactly one device at a time; there is no aggregation on Windows
the way CoreAudio offers on macOS. The routing model assumes one multi-channel
interface, and this is a constraint to design around rather than work around.
