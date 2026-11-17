# erxos-doom

Doom running on erXos as a demo: a fork of
[doomgeneric](https://github.com/ozkl/doomgeneric) with erXos's platform
layer, build and notes committed on top. Not shipped: nothing here
enters the erXos tree or any image erXos's build produces by default;
erXos's tree holds only a submodule gitlink to this repository
(`examples/doom/doom`). doomgeneric is GPL-2.0 (its `LICENSE`).

The history is upstream's, to commit
`e257a09527062bfb986b6e5e0780839b6cbd5b71`, then erXos's commits. No
upstream source file is changed: erXos's platform layer is new files in
`doomgeneric/` beside upstream's other platforms' (`doomgeneric_erxos.c`,
`erxdoom_keys.c`, `erxdoom_keys.h`, `test_keys.c`, `wad.S`), the build
passes `-Dfopen=erxdoom_fopen` to Doom's sources instead of editing them,
and `run.rb`, `app/app.txt`, `deps.lock` and `fetch.rb` are at the top.
Only this README and `.gitignore` differ from upstream's; upstream's
README follows erXos's notes, below.

## Sources

| What | URL | Version | SHA-256 |
|---|---|---|---|
| doomgeneric | https://github.com/ozkl/doomgeneric | commit `e257a09527062bfb986b6e5e0780839b6cbd5b71`, this repository's history | its tree matches the tarball https://codeload.github.com/ozkl/doomgeneric/tar.gz/e257a09527062bfb986b6e5e0780839b6cbd5b71, `26f8f60daa6c6e4d3c01e5eac9fb700d592dede803d13ac848154ff356b2a2e7` |
| Freedoom release zip | https://github.com/freedoom/freedoom/releases/download/v0.13.0/freedoom-0.13.0.zip | v0.13.0 | `3f9b264f3e3ce503b4fb7f6bdcb1f419d93c7b546f4df3e874dd878db9688f59` (matches the release's signed `freedoom-0.13.0-CHECKSUM`) |
| `freedoom1.wad` (from the zip) | | v0.13.0 | `7323bcc168c5a45ff10749b339960e98314740a734c30d4b9f3337001f9e703d` |
| `freedoom2.wad` (from the zip) | | v0.13.0 | `a8772e088847032510d97ba2312406a6998f21cbab44d4ff10696faa9c0ecd4b` |
| Freedoom `COPYING.txt` | | v0.13.0 | `7c62f2c520769c798774f416637eec4921e5f0aafdac1245ae7c8a8cf65fe102` |

Freedoom's license (`COPYING.txt`) is the 3-clause BSD license.
doomgeneric's is GPL-2.0 (its `LICENSE`).

## A deployer's guide: Doom as a third-party native app

This repository is written the way an outside developer would add a native
C app with a window to an erXos machine they deploy. Doom is that example;
it is meant to find out where erXos's app boundary is missing pieces.
Everything erXos-specific a deployer writes is in two files:

* `doomgeneric/doomgeneric_erxos.c`: doomgeneric's platform layer (`DG_Init`,
  `DG_DrawFrame`, `DG_SleepMs`, `DG_GetTicksMs`, `DG_GetKey`,
  `DG_SetWindowTitle`) and `main`. The program is started with a window
  of its own and finds the window's files in its own `/dev`
  (erXos's `docs/design-files.md`, "A program's window"). `DG_Init` reads
  `/dev/ctl`, draw(3)'s 144-byte line on image 0, for the window's
  rectangle and channel (x8r8g8b8), and `DG_DrawFrame` writes Doom's
  640x400 frame to `/dev/draw` at the largest whole scale that fits,
  centred, as `y` loads of image 0 in bands of rows, each write within the
  draw connection's 64 KiB unit and ending on a message, the last band
  ending with a `v` that presents. A thread reads `/dev/kbd`, whose reads
  wait for a record, and turns the held sets of its `k` and `K` records
  into key edges that `DG_GetKey` takes from a ring. Clock and sleep are
  plain `clock_gettime(CLOCK_MONOTONIC)` and `nanosleep`. Doom does not
  follow a resize: it would learn of one only from `/dev/mouse`.
* `doomgeneric/erxdoom_keys.c`: the reader of `kbd` records (a kind byte, runes in
  UTF-8, a NUL), the edges between two held sets, and the table from the
  window's key runes (`abi/formats/input.txt`, `abi/keymaps/us.txt`) to
  Doom's keys, with doomgeneric's X11 bindings: Ctrl fires, Space uses,
  Shift runs, Alt strafes. `test_keys.c` tests them on the host.
* `run.rb`: the app's declaration and build. The app is what
  erXos's `docs/design-apps.md` calls an app: a principal (`doom`), a
  session template (`session doom`), the facet rows that are its authority
  (`facet display`, the display's window tree with an 8 MiB pixel area),
  and a `start` row naming the display, so the start table starts the app's
  session once the display has settled. The session boots the app
  release, and its launcher starts `bin/doom` through a program handler
  (`erx_prog_handler:start/1`) with `window => display`, retrying while
  the display's grant has not arrived, and waits for the handler's
  `'DOWN'`, which carries the program's exit status.

### Build and run

Prerequisites: an erXos checkout or worktree that has run
`make oracle-bootstrap`, the emu oracle
(`ruby tools/erx oracle build --backend native --flavour emu`), the
`llvm-project` and `fsimg` sources (`ruby tools/erx fetch --source ...`) and
`ruby tools/erx build fsimg`; Homebrew LLVM and lld; and
Freedoom as pinned above.

    ruby fetch.rb    # deps.lock's Freedoom into third_party/, SHA-256 checked
    export ERXOS_ROOT=/path/to/erxos \
      ERX_CLANG=/opt/homebrew/opt/llvm/bin/clang ERX_LLD=/opt/homebrew/bin/ld.lld
    ruby run.rb test # the key table, on the host
    gtimeout -k 10 3600 ruby run.rb build < /dev/null > build.log 2>&1
    gtimeout -k 10 900 ruby run.rb boot OUTDIR < /dev/null > boot.log 2>&1

`boot` builds, boots the image under HVF with a virtio-gpu and a virtio
keyboard attached and QMP on an owner-only socket, waits for the command
shell, then plays through QEMU's keyboard (QMP `input-send-event`):
once Doom's title shows, Escape opens the menu and three Enters start a
game on the first episode at the default skill, 0.4 s apart so that all
fall within the title's 170 tics (when the title page ends, Doom starts
its next demo and drops a game asked for in the same tic); then, after
the level has started, Left is held for a second.
It saves a screendump of each step as `OUTDIR/doom-NAME.ppm`, writes the
share of the view (the screen above the status bar) each step changed to
`OUTDIR/play.txt`, and fails unless the menu changed at least 1% of the
view, the view stood still with no key down (at most 2% changed) and the
turn changed at least 25% of it. The serial
log is saved as `OUTDIR/serial.log`. Build output goes to
`$ERXOS_ROOT/build/doom/`.

### Boundary gaps

Each place this app reaches past what an app should need is marked
`BOUNDARY GAP n` in the source. What a third-party app should be given
instead is the input to erXos's packaging design (`docs/design-apps.md`,
"Packaging").

Result (2026-11-15, Apple Silicon macOS, QEMU virt under HVF, 1 CPU,
virtio-gpu scanout 1280x800, erXos master 7ce8ce59a): Doom's 640x400
frame is drawn at scale 2 over the whole window, presented on each frame
end, and QEMU's virtio keyboard plays it: the menu changed 11.7% of the
view, the first level's view changed 1.3% in two seconds with no key down
(the status bar's face excluded) and 89% after a second of Left. No USB
keyboard is involved. The session logs `{doom_running,<10.115.0>}`, the
program handler's pid.

1. **No app SDK or packager.** `run.rb` requires erXos's internal build
   library (`tools/lib/erx`: `NativeRoot`, `NativeRuntime`,
   `ProductionImage`, `Programs::PROGRAM_PREFIX`, `ABI::Codec`) to compile
   against musl's sysroot, link with liberx's program entry
   (`NativeRoot::PROGRAM`, `task.ld`'s fixed addresses) and pack. An app
   needs a published sysroot, link script and program entry, and a tool
   that turns a program plus declared needs into an installable package.
2. **An app's own Erlang code cannot ship.** `ReleaseFiles.domain_boot`
   admits only applications erXos's runtime builds, so the launcher is one
   `-eval` expression in the session template's `args`, written with no
   spaces and no double quotes, and short: the boot page holds a
   template's arguments in 2048 bytes less a 295-byte reserve for node
   names and records, which leaves the expression about 1630 bytes, so its
   names stay short. An app needs its own Erlang applications
   (or at least a launcher module) in its release, or a declared way for
   the system to start its native program with a window without app Erlang
   code at all.
3. **No run-time install.** The app's rows (`limits doom`, `principal
   doom`, `session doom`, `facet display`, `start doom`) are appended to
   the machine's manifest at image build, and the machine-wide sums the
   manifest check proves are grown by hand (`limits display` outer and
   connections, `principal system` memory, `machine root_memory`,
   `max_domains`, `max_connections`). An installed app needs those rows
   declared in its package and admitted against what the build reserved
   for apps, with the sums derived, not edited.
4. **No data files of the app's own.** A native program opens files
   through its namespace (the glue opens its `/dev`), but an app has no
   place of its own for read-only data or a writable directory for config
   and saves, so the 28 MiB IWAD is linked into the binary (`wad.S`) and
   Doom's `fopen` is redirected to `fmemopen` over it.
5. **The program lives in the shared release's `bin/`.** There is one
   domain release, `domains/erxos/`, and every session's programs are its
   `bin/` entries, so an app's program is visible to every session. An app
   needs its own place for its code and data.
6. **App code inflates the boot image.** The 88 MiB single file no longer
   fits below the development machine's root task place (64 MiB), so the
   deployer moves it (`Machine#with(root_mib: 160)`). Installed apps should
   not travel in the boot image at all.
7. **The launcher invents POSIX identity.** `erx_prog_handler:start/1`
   (and `erx_program:start/1` under it) requires `pid`, `ppid`, `pgid`,
   `cwd`, `umask` and signal masks from its caller; the launcher names pid
   100. The session (or the system) should allocate a program's identity.
8. **Presenting and input are relayed by the launcher.** Closed: the
   launcher starts `bin/doom` with `window => display`, and the program
   draws through its own `/dev/draw` and reads its own `/dev/kbd`, with no
   pipe or launcher process between. Each frame now crosses the draw
   connection as `y` loads in writes of up to 64 KiB (67 a frame, about
   4 MB, at scale 2 in a 1280x800 window), a copy the shared buffer did
   not make; whether that keeps up with
   Doom's 35 frames a second is unmeasured.
9. **Waiting for the grant.** The display's grant and pixel area are
   installed after the session starts, so `erx_prog_handler:start/1` with
   a window answers `{error, enodev}` until then and the launcher retries
   (as `erx_pattern_test` polls). A start row that names a display could
   start the session only once its grant is installed, or the start could
   wait.
10. **Restarting a crashed app session is refused.** While the launcher was
    wrong, each crash of `session doom` was followed by a restart refused
    with `{refused, no_memory}` before the next one succeeded, because the
    principal's memory equals one session's wall plus one outer budget;
    the ended domain's budget is not yet given back when the keeper
    retries. App principals need headroom for a restart, or the keeper
    should wait for the ended domain's budget.
11. **No keyboard.** Closed for the demo: the window's `kbd` records reach
    `DG_GetKey` from `/dev/kbd`, read by a thread of the program, and the
    boot run starts a game and turns in it from QEMU's virtio keyboard.
    No USB keyboard is needed.
12. **The client release has no pipe server.** Closed: the session boots
    the domain release's `app` script (`release @artifact.app`), which
    holds `erx_session` beside `erx_win`, as erXos's
    `docs/design-packaging.md` boots a packaged app's session. The
    launcher no longer makes a pipe.
13. **A modifier held before the window had the keyboard stays down.** The
    display sends a window no up for a key whose down went elsewhere, yet
    puts the display's held modifiers in every `k` and `K` record, so Ctrl
    held from before Doom's window took the keyboard reads as Fire until
    the next key record. Harmless here and self-correcting; noted for apps
    that diff the held set as this one does.

---

## doomgeneric's README

# doomgeneric
The purpose of doomgeneric is to make porting Doom easier.
Of course Doom is already portable but with doomgeneric it is possible with just a few functions.

To try it you will need a WAD file (game data). If you don't own the game, shareware version is freely available (doom1.wad).

# porting
Create a file named doomgeneric_yourplatform.c and just implement these functions to suit your platform.
* DG_Init
* DG_DrawFrame
* DG_SleepMs
* DG_GetTicksMs
* DG_GetKey

|Functions            |Description|
|---------------------|-----------|
|DG_Init              |Initialize your platfrom (create window, framebuffer, etc...).
|DG_DrawFrame         |Frame is ready in DG_ScreenBuffer. Copy it to your platform's screen.
|DG_SleepMs           |Sleep in milliseconds.
|DG_GetTicksMs        |The ticks passed since launch in milliseconds.
|DG_GetKey            |Provide keyboard events.
|DG_SetWindowTitle    |Not required. This is for setting the window title as Doom sets this from WAD file.

### main loop
At start, call doomgeneric_Create().

In a loop, call doomgeneric_Tick().

In simplest form:
```
int main(int argc, char **argv)
{
    doomgeneric_Create(argc, argv);

    while (1)
    {
        doomgeneric_Tick();
    }
    
    return 0;
}
```

# sound
Sound is much harder to implement! If you need sound, take a look at SDL port. It fully supports sound and music! Where to start? Define FEATURE_SOUND, assign DG_sound_module and DG_music_module.

# platforms
Ported platforms include Windows, X11, SDL, emscripten. Just look at (doomgeneric_win.c, doomgeneric_xlib.c, doomgeneric_sdl.c).
Makefiles provided for each platform.

## emscripten
You can try it directly here:
https://ozkl.github.io/doomgeneric/

emscripten port is based on SDL port, so it supports sound and music! For music, timidity backend is used.

## Windows
![Windows](screenshots/windows.png)

## X11 - Ubuntu
![Ubuntu](screenshots/ubuntu.png)

## X11 - FreeBSD
![FreeBSD](screenshots/freebsd.png)

## SDL
![SDL](screenshots/sdl.png)
