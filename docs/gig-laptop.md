# Testing on the gig laptop

The band laptop is the machine under test, not a development machine. Nothing
here asks you to build anything on it, and that is deliberate: every service
and toolchain installed on that machine is a potential source of DPC latency,
which is the one thing most likely to make this software glitch on stage. It
should stay as close to clean as it can.

So: **download a binary, install the interface driver, measure.**

---

## 1. Before anything else: is this machine capable?

This is the single biggest risk in the project and it has nothing to do with
the code. A laptop with a badly-behaved driver will produce dropouts no
software can prevent.

Install **LatencyMon** (resplendence.com, free for personal use) and run it
for **ten minutes with WiFi and Bluetooth on**, doing nothing else.

Record:

- the verdict line at the top of the main tab
- **highest measured interrupt-to-process latency** (µs)
- **highest DPC routine execution time** (µs), and which driver it belongs to

Interpreting it:

| highest DPC | meaning |
|---|---|
| under 500 µs | fine, nothing to do |
| 500–1000 µs | usable; small buffers will be tight |
| over 1000 µs | find the driver first. Nothing else is worth doing until this is fixed. |

The usual culprits are WiFi, Bluetooth, and vendor "utility" services. If one
shows up, the fix is normally to disable that device or roll its driver back,
not to change anything in this software.

---

## 2. Install the Behringer UMC ASIO driver

From Behringer's site, for the UMC404HD. **Not** the generic USB-audio class
driver — Windows provides that automatically and it does not expose ASIO.

Confirm it installed as ASIO rather than WDM/MME: the driver's own control
panel should appear, and `btplay --list-devices` should show a `ASIO` entry
once you have the binary.

This step is **not** a prerequisite, contrary to what this document said
first. The claim was that Windows splits the UMC404HD into two stereo
endpoints, so four-output routing needed ASIO. Measured on the real hardware,
the driver exposes a four-channel endpoint under every API:

```
OUT 1-2 (2- BEHRINGER UMC 404HD 192k)   Windows WASAPI   2 ch   48000   3.0ms
OUT 3-4 (2- BEHRINGER UMC 404HD 192k)   Windows WASAPI   2 ch   48000   3.0ms
OUT 1-4 (2- BEHRINGER UMC 404HD 192k)   Windows WASAPI   4 ch   48000   3.0ms
```

`OUT 1-4` is one device and therefore one clock, so front of house on 1/2 with
the click on 3/4 works through WASAPI, and through WDM-KS. Install ASIO
anyway when convenient — exclusive access and lower latency are worth having —
but do not wait on it to start testing.

---

## 3. Get a binary

Do not clone the repository. Either:

- **a tagged release** — the Releases page of
  https://github.com/galoryber/BackingTrackLive, `backingtracklive-windows-x64.zip`
- **or the latest CI build** — the Actions tab, newest green `ci` run, the
  `btrender-windows-latest` artifact

Unzip anywhere. The binaries link the C runtime statically, so there is
nothing to install and no Visual C++ redistributable to chase.

> **Released binaries are WASAPI-only.** The ASIO SDK cannot be committed to
> the repository, so CI cannot build ASIO support. An ASIO-enabled build is
> produced out of band once the Steinberg agreement is signed — see
> [`asio.md`](asio.md). Everything in step 4 can be done without it.

---

## 4. What can be tested today, without ASIO

Plenty, and it is worth doing first because it exercises the entire audio path
on real hardware.

**Enumerate.** The UMC404HD should appear under several APIs:

```
btplay --list-devices
```

Expect the same speakers listed under MME, DirectSound, WASAPI and WDM-KS with
very different latencies. That is normal and is exactly why `device.json` has
an `api` field.

**Make something to play.** The release has no audio in it, and the machine
that plays a show should not need Python installed to try the thing:

```
btrender --make-demo demo
```

That writes stems, a `setlist.json` and a `device.json` into `demo\` — a
complete, runnable set with nothing else to install. Stems default to two
minutes each, which matters for the next step: a machine that drops a buffer
three times in ten minutes reads **zero xruns** over a ten-second clip,
whatever its real behaviour. `--seconds 20` is quicker if you only want to
check routing.

**Play, with real four-channel routing.** Select the `OUT 1-4` endpoint and
put front of house on 1/2 and the click on 3/4:

```json
{
  "device": "OUT 1-4",
  "api": "WASAPI",
  "sample_rate": 48000,
  "buffer_frames": 512,
  "buses": [
    { "name": "foh",   "channels": [0, 1] },
    { "name": "inear", "channels": [2, 3] }
  ]
}
```

The stereo fallback — band mix one side, click the other, split to two mono
feeds at the desk — is still there for an interface that only has two outputs:

```json
  "buses": [
    { "name": "foh",   "channels": [0] },
    { "name": "inear", "channels": [1] }
  ]
```

```
btplay setlist.json device.json 0
```

Watch the **xruns** counter in the status line. Anything but zero means the
audience heard a click.

**Sweep the buffer size.** Run a whole song at each of `128`, `256`, `512`,
`1024` and record the xrun count and the reported output latency:

| buffer_frames | reported latency | xruns over one song |
|---|---|---|
| 128 | | |
| 256 | | |
| 512 | | |
| 1024 | | |

An **xrun** is a buffer underrun: the audio callback failed to deliver samples
before the driver needed them, so the driver played whatever was already in
the buffer. One xrun is one audible click. `btplay` shows a running count
while it plays and prints the total when it stops.

The useful number is the smallest buffer that gives **zero** xruns across a
full song, with a comfortable margin above it — then run one step larger than
that. Remember that latency does not
matter much here — you are not monitoring a live input, and the click and the
tracks are delayed together. Resist tuning it down for its own sake.

**Pull the USB cable mid-song.** This is the one test that cannot be done
anywhere else, and the behaviour is currently *unverified* — the device-loss
detection is written against what PortAudio documents, and nobody has watched
it happen. Expect either a clean "audio device disappeared" message, or
something worse. Either outcome is useful; the second is more so.

---

## 5. Check a real set list

```
btcheck setlist.json device.json
```

Decodes every stem once and reports everything wrong in one pass — missing
files, bus names this machine cannot route, stems that are silent, sample
rates that will be resampled, offsets longer than the stem they move. It
exits non-zero only on things that would actually stop the set playing.

Worth running against your real set list the first time it exists, before
soundcheck rather than during it.

---

## 6. What to report back

The things that cannot be inferred from here:

1. LatencyMon's verdict and the two numbers from step 1
2. The `--list-devices` output, verbatim
3. The buffer-size table from step 4
4. What the USB-unplug actually did
5. Anything that sounded wrong but did not show up as an xrun

That last one matters most and is the hardest to get any other way.

---

## Appendix: should the laptop get a dev environment?

No. It needs a binary, a driver and a measurement tool. Builds happen in CI,
or on a development VM, and the result is copied over.

If driving the laptop remotely would be easier than relaying results by hand,
enabling Windows' built-in OpenSSH server is a light-touch option — it is a
single optional feature, not a toolchain, and it can be stopped before a gig:

```powershell
Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0
Start-Service sshd
Set-Service -Name sshd -StartupType Manual   # not on by default
```

That is a judgement call about a machine on a venue's network, and it is
reasonable to decline it.
