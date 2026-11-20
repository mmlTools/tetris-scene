# Stack Party for OBS Studio

A cozy falling-block game that runs as a native OBS source, built so **your chat plays along**.
Viewers can help you, troll you, throw a party, or take over the controls, and every action
shows their name on stream.

> Requires **OBS Studio 33 or newer** (Windows, macOS, Linux).

## What chat can do

Chat is read anonymously from Twitch IRC, so you never need a login or token. Just type your channel name.

| Team | Commands | What happens |
| --- | --- | --- |
| Trolls | `!flip` `!mirror` `!turbo` `!garbage` `!dark` `!shake` `!wobble` `!curse` | Board turns upside down, controls swap, gravity speeds up, a garbage row appears, lights go out (flashlight mode), earthquake, jelly wobble, and cursed pentominoes with googly eyes |
| Helpers | `!slow` `!bomb` `!long` `!shield` `!sweep` | Chill pill (slow-mo), a bomb that clears a 13-cell blast, the long piece, a shield that blocks the next troll, sweeping the bottom row |
| Party | `!disco` `!hype` `!gg` `!pog` `!lol` `!rip` `!love` `!wow` | Disco lights and confetti, the hype meter, floating reaction bubbles with the viewer's name |
| Help | `!sp` | Shows the command card on stream |

Plain chat messages like `gg`, `pog`, `lol`, `<3` also pop up as reaction bubbles (you can turn this off).

### Stream events
- **Sub / resub / gift sub**: banner, confetti, a **golden piece** (it clears every row it touches) and a shield.
- **Gift bombs**: up to 3 golden pieces plus a confetti storm.
- **Bits**: a thank-you banner. If the bits reach the threshold, the cheer's command runs with no cooldown. A cheer without a command triggers random chaos.
- **Raids**: raid banner, 20 seconds of **HYPE MODE** (double points and disco), and a Chaos Wheel spin.
- **First-time chatters**: a welcome banner.
- **Channel point rewards** that require text: the viewer types a command and it skips the cooldown.

### Meta gimmicks
- **Chaos Wheel**: every accepted chat command fills a meter. When it's full, a slot-machine wheel spins and picks a random effect, a jackpot, or nothing.
- **Hype meter**: `!hype` spam plus your own line clears fill it. When it's full you get double points.
- **Helpers vs Trolls**: a tug-of-war bar plus live **TOP HELPERS / TOP TROLLS** leaderboards. The game-over screen names the MVP helper and the top troll.
- **Chat feed**: every effect is credited on stream, for example "@BOB FLIPPED THE BOARD".
- **Chat Plays**: *Anarchy* (every `!left` `!right` `!rot` `!drop` moves the piece) or *Democracy* (chat votes, live bars, most votes wins).
- **Autopilot / BRB mode**: after an idle time you choose, a bot plays and chat can troll it. If chat mirrors its controls, it gets visibly confused. Press any key to take control back.

### Streamer safety
- Per-viewer and per-command cooldowns. Mods and the broadcaster can bypass them (optional).
- Turn the troll, helper, or party commands on or off as groups.
- The **Chill button** hotkey clears every effect and mutes chat effects for 60 seconds. A second hotkey pauses or resumes chat effects.
- Effect duration slider (25%–300%).

## Install

Download the file for your platform from the latest release.

| Platform | File | Where it goes |
| --- | --- | --- |
| Windows | `tetris-scene-<ver>-windows-x64.zip` | Extract into `C:\ProgramData\obs-studio\plugins\`. The result is `plugins\tetris-scene\tetris-scene.dll` plus `plugins\tetris-scene\data\`. |
| macOS | `tetris-scene-<ver>-macos-universal.pkg` | Run the installer. Or copy `tetris-scene.plugin` into `~/Library/Application Support/obs-studio/plugins/`. |
| Linux (any distro) | `tetris-scene-<ver>-x86_64-linux-gnu.tar.xz` | Extract into `~/.local/share/obs-studio/plugins/`. The result is `tetris-scene/tetris-scene.so` plus `tetris-scene/data/`. |
| Ubuntu (system-wide) | `tetris-scene-<ver>-x86_64-linux-gnu.deb` | `sudo apt install ./tetris-scene-<ver>-x86_64-linux-gnu.deb` |

Restart OBS. The plugin shows up in **Tools → Plugin Manager**.

> Upgrading from 1.x? Delete the old `obs-plugins\64bit\tetris-scene.dll` and
> `data\obs-plugins\tetris-scene` from the OBS program folder, or the old
> `plugins\tetris-scene\bin\64bit` folder. Your existing scenes, settings and hotkeys keep working because the
> source id is unchanged.

## Setup in 60 seconds

1. **Sources → + → Stack Party (chat block game)**.
2. In the properties, under **Chat interaction**, type your **Twitch channel**. That's it.
3. Use the **Try it** buttons to preview subs, raids, bits and the Chaos Wheel.
4. Play:
   - **Interact** (right-click the source → Interact): arrows or WASD to move, Up/W/X to rotate, Z/Q to rotate the other way, Space for hard drop, C or Shift to hold, P or Esc to pause.
   - Or bind global hotkeys in **Settings → Hotkeys → Stack Party** (move, rotate both ways, soft/hard drop, hold, pause, new game, autopilot, chill button, chat toggle).

### Layouts and looks
- Any size works. Wide sources get a three-column layout; tall ones (for example the default 720×1080) get a board plus a sidebar.
- Five themes: **Neon Night**, **Cozy Cafe**, **Retro Arcade**, **Candy Pop**, **Midnight Lo-fi**.
- **Transparent background** turns the game into an overlay on top of your camera or game.

## YouTube, Kick, Streamer.bot, Stream Deck: the local bridge

Turn on **Local bridge** in the source properties. It listens on `127.0.0.1` only, port `8911` by default.

```
GET http://127.0.0.1:8911/cmd?user=NAME&cmd=flip            run a command (no prefix needed)
GET http://127.0.0.1:8911/cmd?user=NAME&text=!bomb          run a raw chat message
     optional: &mod=1  &bypass=1 (ignore cooldowns)  &bits=N
GET http://127.0.0.1:8911/event?type=sub&user=NAME&amount=MONTHS
GET http://127.0.0.1:8911/event?type=gift&user=GIFTER&to=RECIPIENT
GET http://127.0.0.1:8911/event?type=giftbomb&user=GIFTER&amount=COUNT
GET http://127.0.0.1:8911/event?type=raid&user=NAME&amount=VIEWERS
GET http://127.0.0.1:8911/event?type=bits&user=NAME&amount=BITS[&text=!flip]
GET http://127.0.0.1:8911/event?type=follow&user=NAME
GET http://127.0.0.1:8911/event?type=first&user=NAME
GET http://127.0.0.1:8911/state                             JSON game state, for your own overlays
```

If you set a **bridge key**, add `&key=YOURKEY` to `/cmd` and `/event`.

- **Streamer.bot**: add a *Fetch URL* sub-action, for example `http://127.0.0.1:8911/cmd?user=%user%&cmd=flip`, and trigger it from YouTube or Kick chat commands, follows, memberships or Super Chats.
- **Stream Deck / Touch Portal**: point a *Website* or *HTTP GET* button at one of those URLs.

## Building

The project uses the official OBS plugin template build system.

| Platform | Toolchain | OBS dependency |
| --- | --- | --- |
| Windows | Visual Studio 2026 (v18), Windows SDK 10.0.26100 | OBS 33.0.1 sources plus obs-deps `2026-10-23`, downloaded automatically |
| macOS | Xcode 26.5, deployment target macOS 13 | OBS 33.0.1 sources plus obs-deps `2026-10-23`, downloaded automatically |
| Ubuntu | Ubuntu 26.04, Ninja | `obs-studio` from `ppa:obsproject/obs-studio` (33.x) |

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64
cmake --install build_x64 --prefix release/RelWithDebInfo
```

GitHub Actions builds all three platforms on every push. On a version tag (for example `2.0.0`) it creates a draft release with the archives in the layouts above.

## How it works

- A single native source. There's no browser source, so the game runs in recordings and streams with no extra apps.
- Each frame is sent to the GPU in a few batched draw calls; there's no per-pixel draw.
- Chat and bridge networking run on their own threads, so OBS never waits on the network.
- The best score is saved with the scene collection.
