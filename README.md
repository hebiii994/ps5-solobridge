# ps5-solobridge

Play the PS5's audio (games **and** system/menu sounds) on a **Focusrite Scarlett Solo
3rd gen** connected to the console's USB port, on a jailbroken PS5.

The PS5 only accepts USB Audio Class 1 devices: the Scarlett Solo is UAC2-only, so the
console ignores it and only offers HDMI output. `solobridge` is a resident payload that
drives the Solo directly through FreeBSD's `/dev/ugen*` interface and feeds it the
console mix captured from the system capture service.

Tested on **firmware 13.60** with a Scarlett Solo 3rd gen (`1235:8211`).

## Features

- Game audio and system sounds (menu, notifications), also while in game
- Low latency: about 16-40 ms added by solobridge
- Resident: survives unplugging/replugging the Solo and the end of capture sessions
  (e.g. closing a game)
- Single instance, TCP control port (`status`, `stop`), log file
- Audio keeps playing on HDMI too: the capture is a copy of the mix

## Install

Build it (see below) or take `solobridge.elf` from the
[releases](https://github.com/hebiii994/ps5-solobridge/releases), then either:

- send it to the ELF loader (port 9021) with any payload sender, or
- copy it to `/data/pldmgr/payloads/local/solobridge/` for Payload Manager
  (the `installer` payload does this for you, `autoload` adds it to autoload).

Connect the Solo to a PS5 USB port. Sending the payload again while it is running
prints the status of the running instance instead of starting a second one.

```powershell
.\solobridge.ps1 status   # status and statistics
.\solobridge.ps1 stop     # stop
```

Log: `/data/solobridge/log.txt`.

## How it works

**Capture.** `libSceAvcap2` is the client of `av_capture_manager`, the system service
behind recording, broadcasting and Remote Play. Opened with Remote Play's audio source
it returns the full mix (game + system sounds) as 48 kHz stereo float32, in records of
1024 frames. This part comes from
[FGG-PlayPods](https://github.com/FGGstore/FGG-PlayPods) (GPL-3.0), which streams the
same mix to Bluetooth headsets.

**USB output.** The Solo is a UAC2 device: clock source entity, a streaming interface
with a synchronous isochronous OUT endpoint, 2 channels, 24 bit in 4-byte subslots.
solobridge selects alternate setting 1, sets the clock to 48 kHz with a class `SET_CUR`
request and writes one packet per 125 us microframe (6 frames) to the endpoint node
`/dev/usb/BUS.ADDR.EP`. On ugen each `write()` on an isochronous endpoint becomes one
packet.

**Latency.** The kernel FIFO of the endpoint accepts ~800 packets (100 ms). solobridge
keeps it almost empty (~6 ms ahead), estimating consumption from the clock. The ring
buffer is regulated on its minimum level (just before each 1024-frame capture record),
dropping or repeating at most one frame every 8 ms (0.26%, no audible pitch change) to
absorb clock drift. A large excess, like the backlog the capture service hands over
when a session starts, is skipped at once instead of being played back faster.

## Repository layout

| Folder | What it does |
|---|---|
| `solobridge` | The resident payload |
| `installer` | Installs `solobridge.elf` (embedded) in Payload Manager as a local payload |
| `autoload` | Adds `local/solobridge` to `/data/pldmgr/autoload.txt` (with backup, no duplicates) |
| `usbprobe` | Lists `/dev/ugen*` devices and decodes their descriptors (UAC1/UAC2) |
| `solotone` | Plays a 440 Hz test tone on the Solo: the minimal UAC2 streaming example |
| `hello` | Notification sample to check the build/send chain |
| `lsdir` | Lists `/data/pldmgr` showing small `.txt` / `meta.json` files |

Code comments are in Italian.

## Build (Windows + Docker)

The build runs in a Docker image with Ubuntu, clang-18 and
[ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk).

```powershell
docker build -t ps5sdk .
.\build.ps1 solobridge                 # -> solobridge\solobridge.elf
.\send.ps1 solobridge\solobridge.elf   # send to 192.168.88.2:9021, prints stdout
```

`send.ps1` takes `-PS5Host` and `-Port`; `build.ps1 <folder> clean` cleans.
On Linux/macOS just `export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk` and run `make` in a
folder. `installer` embeds `../solobridge/solobridge.elf`: rebuild it after changing
solobridge.

## Notes from testing (FW 13.60)

- Payloads run with `uid=1`; `/dev/ugen*` and `/dev/usb/*` are accessible.
- `USB_GET_DEVICEINFO` is not supported (errno 25) and root hubs `ugenX.1` cannot be
  opened (errno 6): `USB_GET_DEVICE_DESC` / `USB_GET_FULL_DESC` work.
- Before solobridge the Solo reports a 0 Hz sample rate: no system driver touches it.
- Sockets returned by `accept()` inherit `O_NONBLOCK` from the listener (FreeBSD).
- Docker Desktop on Windows may refuse to start with "listening on unix://...: The file
  cannot be accessed by the system" after a forced quit (stale AF_UNIX socket files).
  Quit Docker and rename the folder named in the error, e.g.
  `Rename-Item "$env:LOCALAPPDATA\Docker\run" run.stale`.

## Other UAC2 devices

Only the Solo's VID/PID is matched (`solobridge/solo.c`), but the descriptor parsing is
generic. Other class-compliant UAC2 interfaces with a synchronous or adaptive OUT
endpoint may work by changing `SOLO_VID`/`SOLO_PID`; devices with an asynchronous
endpoint and a feedback endpoint would need feedback handling. Untested.

## License

GPL-3.0 (see `LICENSE`). The capture code is derived from FGG-PlayPods (GPL-3.0),
see `solobridge/NOTICE.md`.
