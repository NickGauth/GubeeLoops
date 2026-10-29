# GubeeLoops

A personal music studio for Windows: patterns, piano roll, playlist, a live Jam View, Chop Shop sampling, drum pads, a mixer, and VST3 plugin hosting. It also includes its own Gubee plugin pack.

## Folder layout (D:\GubeeLoops)

| Folder | What's in it |
|---|---|
| `App\GubeeLoops.exe` | The app. Double-click to start. |
| `Plugins\` | VST3 plugins (the Gubee pack is here). Put downloaded VST3 plugins here too. |
| `Library\Packs\` | Imported sample packs, plus the built-in *Gubee Starter Kit*. |
| `Library\My Chops\`, `Library\Bounces\`, `Library\Recordings\` | Sounds you made in GubeeLoops. |
| `Projects\` | Your songs (`.gubee` files) and `autosave.gubee`. |
| `Exports\` | Rendered WAV / FLAC / OGG files and stems. |
| `Settings\` | Audio settings, plugin list, library settings. |

## Quick start
1. Start `App\GubeeLoops.exe` and press **Space**. The starter beat plays.
2. **TOOLS -> Audio & MIDI settings** picks your sound card. Choose ASIO (if listed) or Windows Audio for low latency.
3. **+ Import Pack** (top of the browser) adds a pack from a folder or `.zip`. You can also drag packs from Explorer onto the browser.
4. Click a sound to hear it. Drag it onto the Channel Rack, a drum pad, the Playlist or a Jam slot.

## The views (shortcut keys)
- **F5 Playlist:** arrange the song. Click to place the current pattern, drag clips, and drag clip edges to trim them. Use **Slice** to cut. Right-click a clip for reverse, pitch, fades, stretch, *Chop at transients*, *Slice to Drum Pads* and *Bounce to audio*. Drag on the ruler to loop a section. Right-click empty space to add an automation clip.
- **F6 Channel Rack:** the step sequencer. Drum-pad channels show one row per pad. Shift-drag a step to change its velocity. **Gubee Drummer** writes beats by genre. **Note Repeat** gives hi-hat rolls.
- **F7 Piano Roll:** draw notes, drag to move, drag the right edge to resize, right-click to delete. Ctrl-drag selects; arrow keys transpose and nudge; Ctrl+Q quantizes. Scale highlighting, chord stamps and **Chord Progressions** help with harmony.
- **F8 Jam View:** an Ableton-style launcher. Put loops or patterns in slots and click to launch them (they start on the next bar). The ▶ Scene buttons launch whole rows. **Capture jam to Playlist** turns your jam into an arrangement.
- **F9 Mixer:** 24 insert tracks plus the master. Each has 10 effect slots (Gubee effects or your plugins), sends and routing.
- **F10 Chop Shop:** sample editor. Trim, cut, reverse, fade, normalize, stretch to tempo and pitch shift. Chop by transients, equal regions, beat grid, manually or with **Lazy Chop** (press L while it plays). Send chops to Drum Pads or Sampler keys, or export them.

## Keys
- **Space:** play / stop.
- **Ctrl+S / Ctrl+O / Ctrl+N:** save / open / new.
- **Ctrl+Z / Ctrl+Y:** undo / redo.
- **Ctrl+R:** export.
- **Ctrl+L:** switch between Pattern and Song mode.
- **Ctrl+E:** arm recording.
- **Ctrl+M:** metronome.
- **Ctrl+B:** show or hide the browser.
- **Computer keyboard as piano:** Z–M (lower octave) and Q–I (upper octave). **[ ]** changes the octave.
- **Drum Pads window:** Z X C V / A S D F / Q W E R / 1 2 3 4 play the 16 pads.

## Plugins
- **TOOLS -> Plugin manager -> Options -> Scan for new or updated VST3 plug-ins.** Each plugin is checked in a separate process, so a broken plugin can't crash GubeeLoops.
- Use instruments with **+ Add** in the Channel Rack. Use effects by clicking a mixer slot.
- The Gubee pack (EQ, Comp, Reverb, Delay, Filter, Crush, Limiter, 808, Synth) also works in other DAWs such as FL Studio.

## Building from source
GitHub Actions builds everything on each push to `master` (see `.github/workflows/build-windows.yml`). The finished binaries are pushed to the `windows-build` branch.

To build locally: `cmake -S . -B build -A x64 && cmake --build build --config Release`. This needs Visual Studio and CMake; JUCE 8 is fetched automatically.
