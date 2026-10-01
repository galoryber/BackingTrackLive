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

## Distributing a build that actually works on our rig

This is a real tension worth stating plainly rather than discovering later.

Public CI cannot build ASIO support, because the SDK cannot live in this
repository. So the binaries attached to a public Release are WASAPI-only.

**Corrected.** This document used to say that Windows presents the UMC404HD
to WASAPI as two separate stereo endpoints, so four-output routing required
ASIO. That came from forum posts rather than from measurement, and it is
wrong for this driver. Enumerated on the band's laptop:

```
OUT 1-2 (2- BEHRINGER UMC 404HD 192k)   Windows WASAPI   2 ch   48000   3.0ms
OUT 3-4 (2- BEHRINGER UMC 404HD 192k)   Windows WASAPI   2 ch   48000   3.0ms
OUT 1-4 (2- BEHRINGER UMC 404HD 192k)   Windows WASAPI   4 ch   48000   3.0ms
```

`OUT 1-4` is a single four-channel device - one device, one clock - so the
drift concern does not arise. Front of house on 1/2 with the click on 3/4
works through WASAPI today, and WDM-KS exposes the same four-channel endpoint.

ASIO is therefore **not** on the critical path. It is still worth having: it
is what the rest of the live-audio world uses, it takes the device
exclusively rather than sharing it with the Windows mixer, and it generally
reaches lower latency. But it is an improvement, not a prerequisite.

The way out, once the Steinberg agreement is signed:

- A separate, manually-triggered workflow fetches the SDK from a private
  location - a URL held in a repository secret - builds with
  `-DBT_ENABLE_ASIO=ON`, and leaves the result as a **run artifact only**.
- That artifact is never attached to a public Release, and the SDK never enters
  the repository, a public artifact, or the git history.
- The public Release stays WASAPI-only and is honest about it: fine for a
  single-device interface, not sufficient for separate click routing on a
  UMC404HD.

That workflow lands with Phase 2, when there is ASIO code for it to build.
Building the machinery before the code exists would only rot.
