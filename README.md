# BackingTrackLive

Backing tracks for a live band, without a DAW in the way.

Load your set list, pick a song, hit the space bar. The click goes to the
in-ears, the backing tracks go to front of house, and nothing surprising
happens at 11pm in a bar.

![The set list](docs/images/stopped.png)

Free and open source, for Windows. [Download the latest
release](https://github.com/galoryber/BackingTrackLive/releases/latest), unzip
it, and run **BackingTrackLive.exe**. Nothing to install.

---

## What it does

**Your set list is a list of songs.** Not one long timeline with markers in it.
Click a song, it plays. Click another, that one plays.

**The click track is separate from the music.** Send the click to the drummer's
in-ears on one pair of outputs and the backing tracks to the desk on another.
Set it up once on screen; it is remembered.

**Songs can run straight into the next one.** Mark a song to segue and it does,
counting in at the new tempo if you want it to.

**You can see what you are about to play.** Song, artist, tempo — and the
tuning, if you keep your guitars in different ones.

![Playing a song](docs/images/playing.png)

The big thing in the middle is the beat. The bar number is in the corner, the
song is top left, and what is coming next is along the bottom. It is readable
from across a stage.

## Getting your songs in

Buy or make your backing tracks however you already do — WAV, FLAC or MP3 all
work. Then:

**1. Make a set list.** Open the program and choose *New set list*. It makes a
folder with one song in it.

**2. Add your stems.** In edit mode, *+ add stem* and pick the file. It copies
into the set list folder, so the whole folder stays something you can put on a
USB stick and hand to the backup laptop.

**3. Tell it the tempo.** Type in the BPM the track was made at. There is no
tempo detection and no time-stretching — this plays your files as they are.

![Editing a song](docs/images/editsong.png)

**4. Line it up.** Downloaded backing tracks almost always start with a bit of
silence before the music, and how much is anyone's guess. Select the stem, hit
*align*, and drag it until it sits on the beat.

![Lining up a stem](docs/images/editalign.png)

The vertical lines are the beats, the brighter ones are bars. The blue is your
stem, drawn where it will actually sound. Drag it, scroll to zoom, and press
play to hear it from wherever you are looking.

If it lines up at the start and stays lined up at the end, you are done. If it
drifts, the tempo you were given is wrong rather than the alignment.

## Sending the click somewhere separate

![Choosing the interface](docs/images/editaudio.png)

*Edit → audio* lists whatever interfaces are plugged in. Pick one, and say
which outputs are front of house and which are the in-ears. On a four-output
interface the usual answer is 1–2 and 3–4, which it fills in for you.

This is a property of the laptop, not of the set list, so you do it once and
every set list you open uses it. It survives upgrading the program.

## During the show

| | |
|---|---|
| `SPACE` | play the selected song |
| `↑` `↓` or click | choose a different song |
| `N` | skip to the next song |
| `B` | put the lights back to the between-songs look |
| `Q` | add a light cue at this bar — only when armed in *edit → lighting* |
| `E` | edit mode (blocked while playing) |
| `F11` | full screen |
| `ESC` | leave full screen — never quits |

When a song finishes, the screen stays put and shows what is next, so one key
starts it.

![Between songs](docs/images/armed.png)

## Lights

If the band runs QLC+, BackingTrackLive can drive it. A cue file sits beside
the set list, saying which MIDI note to send at which bar of which song; QLC+
owns the fixtures, the looks and the DMX, and this owns the timing.

![Lighting cues on a song](docs/images/editsong.png)

A cue is three numbers and a note to self: the **bar**, the **channel** and the
**MIDI note** your desk is listening for. This program has no idea what any of
them mean and no way to find out - that mapping lives in QLC+. Look the number
up there once, type it in here, and it goes out at that bar for ever after.

**The description is shown on stage.** It is never sent anywhere - it is a
note to yourself - but while that look is up its name sits under the song,
with the bar the next change is due on beside it. "Chorus 1" on the screen is
worth having for the band, and it is the quickest way to see that a cue you
placed lands where you thought it would.

**Bar 1 is the first downbeat, after the count-in.** By default the count-in
sends nothing, so the between-songs look stays up while the band hears the
click. If you want a build into the downbeat, bar 0 is the last count-in bar
and bar -1 the one before it - the editor warns if you ask for a bar earlier
than the song actually counts in for, because that cue would never fire.

**Cues are kept in bar order.** Add one you missed and it sorts into place
when you finish typing the bar, not while you type it, and the row stays
highlighted so you can see where it went.

**Copying a song copies its lighting.** That is most of why you would copy
one - the same arrangement with a different vocal, or a cover played twice in
a night.

**You can tap the cues in by ear.** Finding the bar a look should change on
means playing the song and listening for it. Arm `Q` in *edit → lighting*,
play the song, and press it wherever the lights should change; each press
writes a cue at the bar you are in, says so on screen for a bar or two, and
sends nothing at all. Stop, and they are saved for you to give real notes to.
Arming is deliberately forgotten every time the program starts, so a stray `Q`
at a gig does nothing.

![Tapping a cue in](docs/images/tapped.png)

**Stopping sends nothing at all.** Not a blackout, not the between-songs look,
nothing. The band may still be playing the song live, and a stage that goes
dark because somebody touched the laptop is worse than lights that are simply
out of date. That is what `B` is for: when the click has been stopped and the
lights are wherever the song left them, one key puts them back to the
between-songs look. If that look is already up, QLC+'s button toggles it off
instead, which is a blackout - the other thing a hand reaches for at that
moment.

**Seeking asks one question: what should the stage look like here.** Scrubbing
back into a song restores the look that was running at that point. Things that
fire once and are gone - a blinder, fog - stay gone, because a blast of fog
owed from four minutes ago is not a debt worth paying.

The cue file is the one source of truth. Edits here save back into it, the
same way song edits save into `setlist.json`. The show itself is usually built
by a separate tool; the agreement between the two is written down in
[docs/lighting-contract.md](docs/lighting-contract.md) and neither side
changes it alone.

**No cue file means no lights, and everything else works exactly as before.**

## Before the gig

*Edit → check* reads every song and every stem and tells you everything wrong
with the set at once — a missing file, a stem that is silent because the wrong
thing got downloaded, a song pointed at an output your interface does not have.
Better to find out at home.

## What it deliberately does not do

No plugins, no recording, no mixing beyond level and mute, no tempo detection,
no time-stretching. It plays fixed files in a fixed order through fixed
outputs. That is the whole idea: everything it does not do is something that
cannot go wrong on stage.

## Known limits

- **Windows only** in practice. The engine builds and its tests pass on macOS
  and Linux, but there is no UI build for them.
- **No ASIO** in the downloadable builds — the Steinberg SDK cannot be
  redistributed, so released binaries use WASAPI. On a four-output interface
  that still gives four separate outputs, measured at about 22 ms.
  See [docs/asio.md](docs/asio.md).
- **A song that changes tempo partway through** has to have its tempo map
  written into `setlist.json` by hand. See [docs/roadmap.md](docs/roadmap.md).
- **No footswitch yet.** MIDI goes out to the lights; nothing comes back in
  to run the transport hands-free.

## Documentation

| | |
|---|---|
| [docs/gig-laptop.md](docs/gig-laptop.md) | setting up the laptop that plays the show, and where files live |
| [docs/asio.md](docs/asio.md) | ASIO, and why released builds do not have it |
| [docs/lighting.md](docs/lighting.md) | driving QLC+: setting it up, editing cues, proving it without a rig |
| [docs/roadmap.md](docs/roadmap.md) | what is planned, and what is deliberately not |
| [docs/release-notes/](docs/release-notes/) | what changed in each version |
| [docs/development.md](docs/development.md) | building from source, the design, the test suite |
| [docs/resampling.md](docs/resampling.md) | the resampler, measured |

## License

Apache-2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
