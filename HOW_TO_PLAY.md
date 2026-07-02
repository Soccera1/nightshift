# Night Shift Quick Start

Survive each shift until 6 AM without running out of power or letting a threat into the office.

## Core Loop

1. Start a story night from the title screen with `Enter`, controller `A`, or the mouse.
2. Watch the office alerts and camera map for threat movement.
3. Use doors only when Rust or Volt reaches the matching hall entrance.
4. Use hallway lights briefly to confirm nearby hall threats.
5. Open cameras with `Space` or controller `A` to track movement.
6. Watch Camera 6 to repel Skitr from the service vent.
7. Play camera audio with `L`, mouse, or controller left stick to pull Echo back.
8. Conserve power. Doors, lights, cameras, and audio all increase usage.

## Main Controls

- `Left` / `Right`: select unlocked story nights on the title screen
- `A` / `D`: left and right doors
- `Q` / `E`: left and right lights
- `Space`: camera monitor
- `1`-`6`: night select on title, camera select in monitor
- `L`: camera audio while monitor is open
- `C`: phone mute during a shift, Custom Night toggle on title
- `Up` / `Down`: select a Custom Night threat
- `Left` / `Right`: change the selected Custom Night AI value
- `F1`: help overlay
- `M`: mute audio
- `F11`: fullscreen
- `P` / `Esc`: pause or resume during play
- `T`: return from pause to the title screen
- `R`: restart after win or loss
- `E`: Extras after earning a clear star
- `=` / `-`: window scale while windowed

## Controller

- `A`: start or toggle cameras
- `B`: close cameras, return to title, or quit
- `X` / `Y`: doors
- `LB` / `RB`: lights
- Left stick: phone mute or camera audio
- D-pad: select nights, cameras, or Custom Night values
- `Start`: pause
- `Back` / `Guide`: help overlay

## Progression

Winning a story night unlocks the next night through Night 6. Clearing Night 6 awards a gold star and unlocks Extras. Clearing Custom Night with Rust, Volt, Skitr, and Echo all at AI 20 awards a green star.

Custom Night can be started from the title screen or directly:

```sh
./nightshift --custom-night=20,20,20,20
```

Custom Night is separate from story progression. It does not unlock story nights or update best story-night power records.

## Threats

- Rust approaches through the left hall. Close the left door when Rust reaches the entrance.
- Volt approaches through the right hall. Close the right door when Volt reaches the entrance.
- Skitr crawls through the service vent. Watch Camera 6 before Skitr reaches the office.
- Echo follows the audio route and ignores doors. Open the camera monitor and play audio on an earlier camera to pull Echo back.

## Power And Blackout

Doors, lights, camera monitor use, and camera audio increase the usage meter and drain power faster. At 0% power the office blacks out, controls shut down, and you only survive if 6 AM arrives before the outage finishes.

## Files And Troubleshooting

Progress and settings use SDL's per-user preference directory when available. Settings include window mode, mute state, and the last Custom Night AI setup. If SDL cannot provide that path, the game falls back to `nightshift.save` and `nightshift.cfg` in the current directory.

Useful launch options:

```sh
./nightshift --help
./nightshift --fullscreen
./nightshift --mute
./nightshift --reset-save
./nightshift --save=my-profile.save
./nightshift --settings=my-profile.cfg
```

If audio or video fails on a UNIX desktop, confirm SDL2 is installed and try launching from a terminal so SDL error messages are visible. On Win32, keep `SDL2.dll` beside `nightshift.exe`.
