# Night Shift

A small FNAF-inspired fangame written in C23 with SDL2. It targets UNIX-like systems and Win32, uses bundled scene art with procedural UI/effects and audio, and keeps SDL2 as the only runtime library dependency.

Night Shift is an unofficial fan work and is not affiliated with or endorsed by the creators or owners of Five Nights at Freddy's.

See `HOW_TO_PLAY.md` for a compact player guide and `LICENSE` for the project copyright and redistribution notice.

## Gameplay

Survive until 6 AM while conserving power.

- `Left` / `Right`: select unlocked nights from the title screen
- `1`-`6`: select an unlocked night from the title screen
- `A` / `D`: left and right doors
- `Q` / `E`: left and right hallway lights
- `Space`: camera monitor
- `1`-`6`: select cameras while the monitor is open
- `L`: play camera audio while the monitor is open
- `Enter`: start/resume
- `P` / `Esc`: pause/resume during play
- `T`: return from pause to the title screen
- `F11`: toggle fullscreen
- `F1`: show/hide help overlay
- `M`: mute/unmute procedural audio
- `C`: mute the active phone briefing during a night
- `E`: open/close Extras after earning a clear star
- `=` / `-`: increase/decrease window scale while windowed
- `R`: restart after a win/loss
- `Esc`: quit from title/end screens
- Mouse: click title, office, camera, pause, and restart controls
- Controller: `A` start/toggle cameras, `B` close cameras/return to title/quit, `X` / `Y` doors, `LB` / `RB` lights, left stick phone mute or camera audio, D-pad select nights/cameras, `Start` pause/resume, `Back` / `Guide` help
- Custom Night: press `C` on the title screen, then use `Up` / `Down` or D-pad up/down to select Rust, Volt, Skitr, or Echo and `Left` / `Right` or D-pad left/right to set each AI level from 0 to 20. Controller `X` toggles Custom Night from the title screen.

Each shift starts with a short phone briefing that calls out the night-specific threat mix. Rust and Volt are active from the opening nights, Skitr joins on Night 3, and Echo joins on Night 4. Press `C`, click the phone panel, or press controller left stick while cameras are closed to mute it. Doors stop hall threats at the office entrances, lights reveal nearby threats, and the camera monitor lets you track movement. The monitor includes a camera map: green marks the selected camera, red marks active threat presence. When a threat reaches a door, the service vent, or the audio-lure route, the office HUD shows the needed response. The service vent threat is repelled by watching Camera 6 before it enters the office. Echo ignores doors and must be pulled back by playing camera audio on an earlier point of its route. Doors, lights, camera audio, and the monitor all drain power, shown by the usage meter. At 0% power the office blacks out, all systems shut down, and you only survive if 6 AM arrives before the outage ends.

Winning a night unlocks the next one, up to the overtime Night 6; press `Enter`, controller `A`, or click the top end-screen button to continue directly to the next unlocked night. Later nights move threats faster and drain power more aggressively. The title screen shows unlocked and best-cleared night progress, the active threat forecast for the selected story night, and the best remaining-power record once one exists. End screens include a night report with door blocks, vent repels, remaining power, survived hour on losses, and the loss cause plus breaching threat when a shift fails; breach failures also use threat-specific end-screen art. Progress is saved through SDL's per-user preference directory by default, with `nightshift.save` in the current directory as a fallback.

Clearing Night 6 adds a gold title-screen clear star. Clearing Custom Night with Rust, Volt, Skitr, and Echo all set to AI 20 adds a green clear star. These trophies are saved with the same progress file. Once a clear star is earned, Extras unlocks from the title screen with threat dossiers and clear-star status.

Custom Night is a standalone challenge mode. It does not unlock story nights or update best-night progress, and it can also be launched directly:

```sh
./nightshift --custom-night=20,20,20,20
```

The game uses procedural SDL audio for door, light, camera, win, and loss cues, and it generates its runtime window icon in code, so no external asset files are required.

## Build

Install SDL2 development headers, then run:

```sh
make
./nightshift
```

The build checks for SDL2 headers before compiling. If SDL2 is installed outside the compiler's default search paths and `pkg-config` cannot find it, pass `SDL_CFLAGS` and `SDL_LIBS` as shown below.

For a short verification run:

```sh
./nightshift --fast-night
```

To run the deterministic gameplay tests:

```sh
make test
```

To run the full local verification gate:

```sh
make verify
```

This runs logic tests, executable simulations, CLI validation, input/render/screenshot smoke tests, release metadata checks, and staged install/uninstall layout checks.

To run the native verification gate, native package check, and local Win32 helper checks together:

```sh
make release-check
```

This also runs `make package-run-check`, `make win32-model-check`, `make win32-model-run-check`, `make win32-resource-check`, `make win32-dry-run`, and `make win32-package-layout-check`. It still needs a MinGW compiler for the model and resource checks; a full Win32 SDL build additionally needs a MinGW SDL2 SDK.
The GitHub Actions workflow runs the same UNIX release gate and a real MSYS2/UCRT64 Win32 SDL build, verifies the produced Win32 archive checksums and contents, runs the packaged `.exe` from both archives under a native Windows PowerShell step, then uploads `nightshift-unix` and `nightshift-win32` release archives as workflow artifacts.

To prove the release can be regenerated from source after removing build outputs:

```sh
make clean-release-check
```

To exercise keyboard, mouse, and controller input handlers without needing a physical controller:

```sh
make input-test
./nightshift --input-test
```

To exercise settings load/save and Custom Night AI persistence through the executable:

```sh
make settings-test
./nightshift --settings=/tmp/nightshift-settings-test.cfg --settings-test
```

To check that SDL rendering produces a nonblank frame without opening a real window:

```sh
make render-test
./nightshift --render-test
```

To check that every procedural sound cue can be queued through SDL audio:

```sh
make audio-test
./nightshift --audio-test
```

To save a deterministic BMP frame for visual inspection:

```sh
make screenshot-test
./nightshift --screenshot=nightshift-frame.bmp
./nightshift --screenshot-scene=loss-skitr --screenshot=skitr-loss.bmp
```

Screenshot scenes are `title`, `title-cleared`, `extras`, `office`, `camera`, `win`, `loss-rust`, `loss-volt`, `loss-skitr`, `loss-echo`, and `blackout`. `make screenshot-test` verifies every generated BMP is nonblank and exactly 960x540.

To show command-line options:

```sh
./nightshift --help
./nightshift --version
```

Unknown options and invalid option values exit with an error before SDL starts.

To run full-night executable simulations without opening SDL video/audio:

```sh
make simulate
./nightshift --fast-night --simulate=defended
./nightshift --fast-night --night=6 --simulate=defended
./nightshift --fast-night --simulate=idle
./nightshift --fast-night --custom-night=20,20,20,20 --simulate=defended
```

To install locally:

```sh
make install PREFIX=$HOME/.local
```

On UNIX-like desktops this installs `nightshift.desktop`, an SVG app icon, and AppStream metadata under the selected prefix. For Win32 installs, if `SDL_PREFIX`, `SDL_DLL`, or the MinGW compiler directory points at `SDL2.dll`, it is installed beside `nightshift.exe`.

To verify the install layout in a temporary root without touching the real prefix:

```sh
make install-check
make uninstall-check
```

To create a release archive:

```sh
make package
make package-check
make package-run-check
```

The archive is written to `dist/nightshift-0.1.0-unix.tar.gz` by default, with a matching `.sha256` checksum. UNIX archives include desktop launcher, icon, generated AppStream metadata, a compact player guide, a generated `PACKAGE.txt` manifest, and the project license notice. Win32 builds produce `nightshift-0.1.0-win32.zip` and a self-contained native setup executable at `nightshift-0.1.0-win32-setup.exe`; no Win32 `.tar.gz` is generated. The Win32 zip includes `nightshift.exe`, `SDL2.dll`, `WINDOWS.txt`, the player guide, package manifest, and license notice; the setup executable embeds the same files as Win32 resources and installs them to `%LOCALAPPDATA%\Night Shift` by default without NSIS, WiX, or another installer framework. The setup UI has a scaled install-location field and Browse button, and scripted installs can pass `/S /D=C:\Path\To\Night Shift`. `make package-check` verifies the checksum and expected archive contents, and rejects local save/config files, screenshots, temporary files, and build objects. `make package-installer-check` builds and verifies the Win32 setup executable. `make package-run-check` extracts the native UNIX archive and runs its packaged `--version` and `--help` commands; for `TARGET=win32`, it runs the packaged `.exe` from the Win32 zip on MSYS2/MinGW/Cygwin hosts and skips on non-Windows cross-build hosts. Override `VERSION=x.y.z` to label local builds; release checks require numeric `major.minor.patch` values so Win32 version resources and generated AppStream metadata remain valid. For a Win32 zip or installer, build with `TARGET=win32`; `SDL_PREFIX`, `SDL_DLL`, or the MinGW compiler directory must point at `SDL2.dll` so it can be copied into the package beside `nightshift.exe` and embedded into the setup executable.
Win32 builds also compile generated version metadata, a generated application icon, and an application manifest into `nightshift.exe` when `windres` is available.

Or choose a custom night length in seconds:

```sh
./nightshift --night-seconds=45
```

To select and unlock a specific story-mode night at startup for testing:

```sh
./nightshift --night=4
```

Do not combine `--night` with Custom Night.

To use a separate save file:

```sh
./nightshift --save=my-profile.save
```

To reset progress in the selected save file:

```sh
./nightshift --reset-save
./nightshift --save=my-profile.save --reset-save
```

Window, audio, and last-used Custom Night AI settings are saved through the same per-user preference directory by default, with `nightshift.cfg` in the current directory as a fallback. To use a separate settings file or launch with a specific scale/fullscreen mode:

```sh
./nightshift --settings=my-profile.cfg
./nightshift --scale=2
./nightshift --fullscreen
./nightshift --mute
```

The Makefile automatically uses `-std=c23` when supported and falls back to `-std=c2x` for compilers that still use the draft spelling. To force a specific standard flag, use:

```sh
make CSTD=c2x
```

For MinGW/Win32:

```sh
git submodule update --init vendor/SDL
make sdl2-win32
make TARGET=win32 CC=x86_64-w64-mingw32-gcc
make TARGET=win32 CC=x86_64-w64-mingw32-gcc package
make TARGET=win32 CC=x86_64-w64-mingw32-gcc package-installer
```

`make sdl2-win32` builds the SDL2 submodule with the selected MinGW compiler and installs headers, import libraries, and `SDL2.dll` under `build/sdl2-win32`. The SDL submodule is pinned to the SDL2 `release-2.32.8` tag.

If the MinGW compiler is installed but the SDL2 cross-development package is not, you can still compile the non-SDL game model and save/settings logic, including Win32-specific atomic replacement code:

```sh
make win32-model-check
```

If Wine is installed and runnable, the same Win32 model test executable can be run under Wine:

```sh
make win32-model-run-check
```

This check skips when Wine is unavailable, so `make release-check` remains usable on UNIX builders that only have the MinGW compiler.

You can also compile the generated Win32 version, icon, and manifest resource without the SDL2 cross SDK:

```sh
make win32-resource-check
```

This check also inspects the compiled resource object for the expected icon, group icon, version, and manifest entries.

To locally validate the Win32 verify/package command graph without an SDL2 cross SDK, run the dry-run target. It uses a temporary fake `SDL2.dll` only to prove the packaging commands would include the DLL when one is provided, and it checks that the planned Win32 link uses the SDL libraries, key Windows system libraries, and compiled resource object:

```sh
make win32-dry-run
```

To locally validate the Win32 release archive shape and archive hygiene rules without an SDL2 cross SDK, run:

```sh
make win32-package-layout-check
```

If Wine is runnable and a Win32 SDL2 package has been built, you can also run the packaged Win32 `.exe` from both archives and exercise SDL render/audio through Wine:

```sh
make TARGET=win32 CC=x86_64-w64-mingw32-gcc SDL_PREFIX=/path/to/mingw-sdl2 win32-wine-package-run-check
```

For the submodule-built SDL2 runtime, use the vendored helpers:

```sh
make win32-vendored-package-check
make win32-vendored-wine-package-run-check
make win32-vendored-release-check
```

To run the full Win32 SDL build, verification, package check, and packaged executable check when a MinGW SDL2 SDK is available, run:

```sh
make WIN32_CC=x86_64-w64-mingw32-gcc WIN32_PKG_CONFIG=x86_64-w64-mingw32-pkg-config win32-sdl-probe
make WIN32_CC=x86_64-w64-mingw32-gcc WIN32_PKG_CONFIG=x86_64-w64-mingw32-pkg-config win32-sdl-release-check
```

`make win32-sdl-probe` verifies the selected MinGW compiler can compile and link a tiny SDL2 executable and that a real `SDL2.dll` can be found for packaging.

If the SDL2 package is installed in a separate cross-compilation prefix, prefer `SDL_PREFIX` so both headers, libraries, and `SDL2.dll` packaging are derived from the same tree:

```sh
make TARGET=win32 CC=x86_64-w64-mingw32-gcc SDL_PREFIX=/path/to/mingw-sdl2
```

Or, with a cross-aware pkg-config:

```sh
make TARGET=win32 CC=x86_64-w64-mingw32-gcc WIN32_PKG_CONFIG=x86_64-w64-mingw32-pkg-config
```

If `pkg-config` is not available on UNIX, override `SDL_CFLAGS` and `SDL_LIBS`:

```sh
make SDL_CFLAGS="-I/path/to/SDL2/include" SDL_LIBS="-L/path/to/SDL2/lib -lSDL2"
```

For manual MinGW/Win32 overrides, include SDL2's Win32 entry-point libraries and point `SDL_DLL` at the runtime DLL used for packaging:

```sh
make TARGET=win32 CC=x86_64-w64-mingw32-gcc \
  SDL_CFLAGS="-I/path/to/mingw-sdl2/include/SDL2" \
  SDL_LIBS="-L/path/to/mingw-sdl2/lib -lmingw32 -lSDL2main -lSDL2" \
  SDL_DLL=/path/to/mingw-sdl2/bin/SDL2.dll
```
