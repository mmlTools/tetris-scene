# OBS Tetris Plugin

Play Tetris directly inside OBS Studio as a source in your scenes.
Useful for livestreams, waiting screens, or interactive content.

## Features

- Playable Tetris inside OBS
- Live Score, Level, Lines, Timer
- Next piece preview
- Pause and Game Over messages
- Lightweight and smooth (no external UI)

## Installation

### Windows
1. Download the plugin release .zip
2. Extract into your OBS installation folder:
   C:\Program Files\obs-studio\
3. Restart OBS Studio

### Linux
1. Build or install the plugin
2. Copy the .so file into:
   ~/.config/obs-studio/plugins/
3. Restart OBS Studio

## Add Tetris to Your Scene

1. Open OBS
2. Go to Sources
3. Click + (Add)
4. Select "OBS Tetris"
5. Resize and position as needed

## Controls (Hotkeys)

You must configure controls manually:

1. Go to Settings → Hotkeys
2. Find the OBS Tetris section
3. Bind keys for:

- Move Left
- Move Right
- Rotate
- Soft Drop
- Hard Drop
- Pause
- Reset

Tip: Use comfortable keys like:
- Arrow keys (movement)
- Up (rotate)
- Down (drop)
- Space (hard drop)
- P (pause)

## How It Works

- The game runs as a native OBS source
- No browser sources or external apps needed
- Works in recordings and livestreams

## Notes

- Game only responds to configured hotkeys
- Make sure OBS is focused when playing
- Reset is required after Game Over

## Use Cases

- Stream starting screens
- Intermission content
- Chat challenges / engagement
- General stream interaction
