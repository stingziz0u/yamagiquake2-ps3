# YamagiQuake2-PS3

A native homebrew port of [Yamagi Quake II](https://github.com/yquake2/yquake2)
for the PS3, with a hardware (RSX) OpenGL renderer and both official mission
packs built in.

## Lineage

Yamagi Quake II is an actively maintained, conservative Quake II client
built on Icculus Quake II, itself based on id Software's Quake II 3.21
source release. This project adds a full PS3 platform layer on top of it
(video, audio, input, file system, system integration) and a small OpenGL
translation layer so Yamagi's own OpenGL renderer runs on the RSX. In short:

```
Quake II (id Software, 1997) -> Icculus Quake II -> Yamagi Quake II -> YamagiQuake2-PS3 (this project)
```

The upstream README (features, cvars, console commands) is still valid for
everything that isn't PS3 specific:
[yquake2/yquake2](https://github.com/yquake2/yquake2/blob/master/README.md).

## Features

- **Two renderers**, switchable from the Video menu:
  - **OpenGL (GPU)**, the default: Yamagi's `ref_gl1` renderer running on
    the RSX through *ps3gl*, a GL 1.x subset written for this port (based
    on the GL-to-RSX layer from [IoQuake3-PS3](https://github.com/Mayo1970/IoQuake3-PS3)).
    Runs at 60 fps at 1280x720 internal resolution, with multitextured
    lightmaps, swizzled mipmapped textures and a triple-buffered frame
    pipeline.
  - **Software (CPU)**: Yamagi's `ref_soft`, for the classic look. 640x480
    or 848x480 are the sensible resolutions for it.
- **Video**: 720p output. Selectable internal render resolution, scaled to
  the TV by the RSX: 4:3 (320x240, 400x300, 512x384, 640x480) and 16:9
  (640x360, 768x432, 848x480, 960x540, 1280x720). TV screen fit (overscan),
  smooth or sharp scaling, brightness and gamma applied live by the GPU,
  frame rate up to 60 or locked at 30, HUD scale, and an optional FPS
  counter (top left or top right).
- **Audio**: 48 kHz hardware audio on its own thread, OGG music (the GOG
  soundtrack files, see [Installation](#installation)) and the original
  cinematics with sound.
- **Input**: DualShock 3, with every button rebindable from the in-game
  menus. Look sensitivity goes from 0.5 to 8 in 0.5 steps, plus Yamagi's
  advanced stick settings (yaw/pitch speed, ramp time, invert pitch,
  sticks layout).
- **Mission packs**: *The Reckoning* and *Ground Zero* are linked into the
  same executable. Drop in their data and pick them from the menu. Save and
  load work in both.
- **Mods**: data mods from a dedicated `USRDIR/mods` folder (see
  [Known limitations](#known-limitations) for what that covers).
- Saves and settings on the HDD, a crash/diagnostic log capped in size,
  and a text error screen instead of a silent black screen if something
  fatal happens at boot.

## Requirements

- A PS3 capable of running homebrew (HEN or CFW).
- Your own legally-owned copy of Quake II (and the mission packs if you
  want them). **No game data is included in this repository or in any
  release build.** Use the original game's data (CD, GOG or the original
  Steam release). The 2023 re-release's data is not supported.
- To build from source: the [ps3dev / PSL1GHT](https://github.com/ps3dev)
  toolchain (`ppu-gcc`, PSL1GHT SDK, `make_self_npdrm`, `package_finalize`,
  etc.), `git` and `python3`.

## Installation

1. Install `quake2ps3-1.0.pkg` from the XMB (shows up as "Quake II").
   This creates `/dev_hdd0/game/QUAKE2PS3/USRDIR/` with every folder below
   already in place, empty and ready to fill.
2. By FTP, copy your game data there:

   ```
   QUAKE2PS3/USRDIR/baseq2/pak0.pak      <- required
   QUAKE2PS3/USRDIR/baseq2/pak1.pak      <- if your version has it
   QUAKE2PS3/USRDIR/baseq2/pak2.pak      <- if your version has it
   QUAKE2PS3/USRDIR/baseq2/video/        <- optional, the .cin cinematics
   QUAKE2PS3/USRDIR/music/               <- optional, Track02.ogg .. Track21.ogg (GOG)
   ```

   Don't replace `baseq2/yq2.cfg`: it holds the PS3 defaults (controls,
   renderer, resolution).

3. **Mission packs** (optional), each with its own `pak0.pak` and, if you
   have them, its cinematics:

   ```
   QUAKE2PS3/USRDIR/xatrix/pak0.pak      <- The Reckoning
   QUAKE2PS3/USRDIR/xatrix/video/
   QUAKE2PS3/USRDIR/rogue/pak0.pak       <- Ground Zero
   QUAKE2PS3/USRDIR/rogue/video/
   ```

   They show up in **Game -> mods**. Their music comes from the same
   `USRDIR/music/` folder (GOG's Track12-21 are Ground Zero's; The Reckoning
   reuses the base game's tracks).

4. **Mods** (optional): one folder per mod inside `USRDIR/mods/`, e.g.
   `QUAKE2PS3/USRDIR/mods/mymod/pak0.pak`. They show up in **Game -> mods**
   too. Read the mods entry under [Known limitations](#known-limitations)
   first.

**Filenames are case-sensitive** on the PS3: keep them exactly as your
install has them (`pak0.pak`, `Track02.ogg`).

Saves and `config.cfg` live in `/dev_hdd0/data/quake2/<game>/` (for
example `/dev_hdd0/data/quake2/baseq2/save/`), so reinstalling or updating
the PKG never touches them. The log is `USRDIR/quake2_log.txt` (the
previous boot's is kept as `quake2_log.old.txt`; each is capped at 512 KB).

## Building from source

1. Install the [ps3dev toolchain](https://github.com/ps3dev/ps3toolchain)
   (third-party, not part of this project -- these are its own official
   instructions):

   ```bash
   git clone https://github.com/ps3dev/ps3toolchain.git
   cd ps3toolchain
   export PS3DEV=/usr/local/ps3dev
   export PSL1GHT=$PS3DEV
   export PATH="$PATH:$PS3DEV/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin"
   ./toolchain.sh
   ```

2. Clone this repository and build, from its root:

   ```bash
   git clone https://github.com/stingziz0u/yamagiquake2-ps3.git
   cd yamagiquake2-ps3
   make -C ps3 check-toolchain   # optional: checks the toolchain is usable
   make -C ps3 addons            # once: fetches the mission packs' game code
   make -C ps3 -j4               # -> ps3/quake2ps3-1.0.pkg
   ```

`make -C ps3 addons` clones Yamagi's [xatrix](https://github.com/yquake2/xatrix)
and [rogue](https://github.com/yquake2/rogue) repositories into `ps3/addons/`
at pinned commits and applies the small fixes in `ps3/addon-fixes/`. It needs
internet access only that one time.

The fragment and vertex programs are already compiled into
`src/backends/ps3/gl/ps3gl_shader_data.h`. Rebuilding them
(`src/backends/ps3/gl/shaders/build_shaders.sh`) needs NVIDIA's Cg compiler
and PSL1GHT's `cgcomp`, and is only necessary if you change the shaders.

`make -C ps3 clean` removes the object files. `make -C ps3 STAGE=1` builds
a headless test PKG (engine and game only: loads a map, changes level and
quits, logging everything) that was used to bring up the port.

### Personal build with Zaero

Team Evolve's *Zaero* mission pack works too, but its game code is under
id Software's *Limited Program Source Code License* (the Quake II SDK
license), not the GPL, so it can't be linked into a GPL program that is
distributed. It is therefore **never part of a release PKG**. You can build
it into your own copy:

```bash
make -C ps3 addons ZAERO=1
make -C ps3 -j4 ZAERO=1          # -> ps3/quake2ps3-1.0-zaero.pkg
```

Then put Zaero's `.pak` files in `USRDIR/mods/zaero/` and its cinematics in
`USRDIR/mods/zaero/video/`. Don't share the resulting PKG.

## Controls

| Action | Default | Notes |
|---|---|---|
| Move / Strafe | Left stick | |
| Look | Right stick | |
| Jump / Swim up | Cross | |
| Crouch / Swim down | Circle | |
| Attack | R2 | |
| Walk | L2 or L3 (hold) | Always run is on |
| Previous / Next weapon | L1 / R1 | |
| Last weapon | D-pad up | |
| Quicksave | D-pad down | Load from Game -> load game |
| Previous / Next item | D-pad left / right | |
| Use item | Square | |
| Inventory | Triangle | |
| Center view | R3 | |
| Help computer | Select | |
| Menu | Start | Fixed, not rebindable |
| Confirm / Back (menus) | Cross / Circle | |

Everything except Start is rebindable from **Options -> customize gamepad
-> customize buttons**, where the stick settings and look sensitivity are
too.

## Known limitations

- **Mods: data only.** On PC a Quake II mod can be data (maps, models,
  sounds in `.pak`/`.pk3` files) and/or new game code (a `gamex86.dll` /
  `game.so`). The PS3 can't load PC libraries at all, and homebrew can't
  load game code at runtime either, so the base game, The Reckoning and
  Ground Zero are compiled into the executable. What that means in
  practice:
  - A mod that is only data works: put its folder in `USRDIR/mods/`.
  - The mod needs at least one `.pak` or `.pk3` file to be listed in
    **Game -> mods**. If it comes as loose files, zip them (keeping the
    folder structure: `maps/`, `models/`, ...) and rename the `.zip` to
    `pak0.pk3`.
  - A mod with its own game code runs the **base game's** code instead:
    its maps may load, but anything the mod's code adds (weapons,
    monsters, rules) is missing or broken. Such a mod only works if its
    source code is available and it gets compiled into the executable,
    the way Zaero can be (see [Personal build with Zaero](#personal-build-with-zaero)).
  - `.dll`/`.so` files in a mod folder are ignored.
- **Single player only.** Networking is loopback only: no LAN or online
  play, no server browser. Capture The Flag isn't included.
- **DualShock 3 only.** No USB keyboard or mouse (so no console), no
  motion (gyro) aiming, and no rumble.
- **Screenshots** (`screenshot` command) come out black with the OpenGL
  renderer: reading the RSX's memory back isn't implemented.
- The 2023 re-release's data isn't supported, only the original game's.

## Credits

- [Yamagi Quake II](https://github.com/yquake2/yquake2) by Yamagi Burmeister
  and contributors -- the engine this project is built on, including its
  [xatrix](https://github.com/yquake2/xatrix) and
  [rogue](https://github.com/yquake2/rogue) game code (and the
  [zaero](https://github.com/yquake2/zaero) port for personal builds).
- id Software, for releasing the Quake II source code under the GPL.
  Xatrix Entertainment (*The Reckoning*), Rogue Entertainment (*Ground
  Zero*) and Team Evolve (*Zaero*) for the mission packs.
- [IoQuake3-PS3](https://github.com/Mayo1970/IoQuake3-PS3) by Mayo1970 --
  ps3gl, this port's OpenGL-to-RSX layer, started as a fork of its GL
  layer (state tracking, vertex format, shaders, GCM usage).
- [PSL1GHT](https://github.com/ps3dev/PSL1GHT) and
  [ps3toolchain](https://github.com/ps3dev/ps3toolchain) -- the PS3
  homebrew SDK and toolchain.
- [SDL](https://github.com/libsdl-org/SDL) -- key code tables in the SDL
  compatibility headers (zlib license).
- DejaVu Sans Mono (Bitstream Vera license) -- the 8x16 font of the error
  screen, taken from Doom64-PS3.

## License

GPLv2, inherited from Yamagi Quake II and, through it, id Software's
Quake II source release. See [`LICENSE`](LICENSE), which also lists the
third-party code Yamagi Quake II bundles and its licenses.

The mission packs' game code is fetched at build time from Yamagi's
repositories (GPLv2 for xatrix and rogue). Zaero's code is not GPL and is
never distributed with this project, see
[Personal build with Zaero](#personal-build-with-zaero).

No Quake II data files (`.pak`, music, cinematics or otherwise) are
included in this repository. You need your own legally-owned copy of the
game.

## AI disclosure

This project's code was written collaboratively with Claude (Anthropic),
working through this port with me in real time over many sessions.
Every bit of testing and debugging were made by me on real hardware.
