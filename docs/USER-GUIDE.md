<!--
SPDX-FileCopyrightText: 2026 jerbanpls
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Player's guide

This guide is for playing the port. Developer topics, such as building from source, the
tests and the recompiler, are in the [README](../README.md) and the other files in
`docs/`.

## Legal note

**No game data comes with this program.** The AppImage and the Flatpak hold only the
engine: the runtime, the renderer and the tools that turn a ROM into game code. They
contain no ROM, no game code, no graphics, no music and no text from the game, and none
of those is distributed by this project.

You supply your own copy of the game, dumped from a cartridge you own. On the first
launch, the engine translates the code in that ROM into a "game module" (`ct_game.so`)
**on your computer**. The module is built from your ROM, so it counts as game data: it
stays on your machine, nothing is uploaded anywhere, and you must not share it, just as
you must not share the ROM.

## What you need

- **A Linux PC** with x86-64 (64-bit Intel or AMD), such as a desktop, a Steam Deck or a
  ROG Ally running Linux. Windows and macOS are not supported yet.
- **Your own ROM dump** of the North American (US) 1.0 release of the game, in this
  format:
  - **headerless**, exactly **4,194,304 bytes** (4 MiB)
  - SHA-1 `de5822f4f2f7a55acb8926d4c0eaa63d5d989312` (CRC32 `2D206BF7`)
  - The file extension doesn't matter (`.sfc` and `.smc` are both fine); only the
    contents are checked.

  A file of 4,194,816 bytes has a 512-byte copier header. Remove the header, for example
  with `tail -c +513 dump.smc > dump.sfc`, and use the new file. Other regions,
  revisions, translations and ROM hacks don't match the checksum and won't be accepted.
- **Some free disk space** in your home folder while the module is built: the
  generated C files and objects are written to the data folder (see
  [Where your files live](#where-your-files-live)) and deleted once the module is linked.

The packages bundle everything the module build needs: a C compiler (zig) in both, and
Python in the AppImage. The Flatpak uses the Python in its runtime.

**On a Steam Deck or ROG Ally**, see [HANDHELD.md](HANDHELD.md): a step-by-step guide for
Game Mode, with no keyboard needed after setup.

## Installing

### AppImage

1. Get `CT_Recomp-x86_64.AppImage` from a release, or build it yourself with
   `packaging/appimage/build.sh` (see [PACKAGING.md](PACKAGING.md)).
2. Make it executable, either in your file manager's Properties dialog or with:
   ```
   chmod +x CT_Recomp-x86_64.AppImage
   ```
3. Double-click it, or run `./CT_Recomp-x86_64.AppImage`.

If it complains about FUSE, run it as
`./CT_Recomp-x86_64.AppImage --appimage-extract-and-run`.

The AppImage built with `packaging/appimage/build-container.sh` needs glibc 2.35 or
newer (Ubuntu 22.04, Debian 12, Fedora, Arch, SteamOS 3 and Bazzite all qualify). An
image built directly on a very new distribution needs that distribution's glibc, so
use a release image or the container script. The module is built on your machine, so
it needs about 1 GB of free space and a minute or two the first time.

### Flatpak

The Flatpak isn't on Flathub. Build and install it from the source tree as described in
[PACKAGING.md](PACKAGING.md), or, if you were given a `.flatpak` bundle file:

```
flatpak install --user ct-recomp.flatpak
flatpak run io.github.dittobyte.CtRecomp
```

After that, it also appears in your desktop's application menu as **CT Recomp**.

The Flatpak can read only your **Downloads** and **Documents** folders, so keep the ROM in
one of those. To use another folder, grant read-only access first:

```
flatpak override --user --filesystem=/path/to/roms:ro io.github.dittobyte.CtRecomp
```

### Options at launch

Engine options go after the program, for example `--hd2d` to turn on the HD-2D look
(see [The HD-2D look](#the-hd-2d-look)):

```
./CT_Recomp-x86_64.AppImage --hd2d
flatpak run io.github.dittobyte.CtRecomp --hd2d
```

| Option | What it does |
| --- | --- |
| `--help` | Print the options and the keyboard controls, then quit |
| `--rom FILE` | Use this ROM file |
| `--hd2d` | Draw with the HD-2D renderer (off by default, also in Steam's Game Mode; the menu's Look tab can turn it on and remembers it) |
| `--no-hd2d` | The original look, even if the menu saved HD-2D |
| `--fullscreen` | Start fullscreen |
| `--scale N` | Window size, as a multiple of the console's 256×224 picture (default 3). Without it, the size you last left the window at is used |
| `--interp-only` | Don't build or load the game module; run everything in the interpreter |
| `--preset off\|subtle\|hd2d\|deck` | Starting HD-2D look (with `--hd2d`); `deck` is a lighter HD-2D, the default on a Steam Deck |
| `--aspect 4:3\|pixel\|stretch` | Picture shape (with `--hd2d`) |
| `--log-gpu` | Print the HD-2D renderer's frame time every 60 frames |
| `--pacing auto\|timer\|vsync` | How frames are paced: the engine's 60.0988 Hz timer (`timer`, and `auto`, the default), or the display's vsync on a 60 Hz screen (`vsync`, see [HANDHELD.md](HANDHELD.md#looks-frame-rate-and-battery)) |
| `--log-pacing` | Print frame-time statistics every 60 frames (every exit prints them for the whole run) |
| `--save FILE` | Keep the game's save memory in FILE instead of `ct/ct.srm` (see [Your saves](#your-saves)) |
| `--no-save` | Don't load or write the save file: in-game saves last only until you quit |
| `--pack NAME` | Load an asset pack (replacement art; repeat to stack packs, first wins), remembered for next time; `--pack none` loads none (see [Asset packs](#asset-packs)) |
| `--pack-dump NAME` | While you play, save each new tile and sprite into asset pack NAME |
| `--rumble off\|light\|medium\|strong` | Rumble strength for this run (see [Rumble](#rumble)); `--log-rumble` prints every pulse |
| `--music-log` | Print each song the game asks for (its id, for [audio packs](#audio-packs)), and its fades and stops |
| `--hd-font FILE` | Draw the text boxes with this font (see [HD text](#hd-text)), remembered for next time |
| `--pack-export-sheets NAME` | While you play, save every character pose seen onto sprite sheets in `sheets/NAME`, to paint over and import (`--pack-export-scale N`: their second size, default 4x) |

## First launch

1. **Choose the ROM.** The engine looks for a ROM in this order:
   1. `--rom FILE`
   2. the `CT_ROM` environment variable
   3. the ROM that worked last time
   4. a `.sfc` or `.smc` file of the right size and checksum in `~/Documents`,
      `~/Downloads`, the AppImage's folder or the data folder (up to two subfolders
      deep, at most about 2000 files)

   If none of those works, the window says where to put the ROM, with its size and
   SHA-1. Copy the file there and press **A** (or Enter) to search again, or **drop the
   file on the window** from your file manager. When a file is rejected, the screen says
   why (see [Troubleshooting](#troubleshooting)).
2. **The module build.** The first time, and again whenever the ROM or the engine
   changes in a way that needs it, the engine builds the game module. A progress screen
   lists each step as it runs, with a seconds counter at the bottom:
   1. checking the ROM
   2. decoding overlays
   3. translating to C
   4. compiling
   5. linking
   6. done (the generated C has been deleted by then)

   This takes about a minute (around 40 seconds on a ROG Ally), and happens once.
   **Ctrl+Q** (in Game Mode, the Steam button) stops the build and quits.
3. **Play.** The screen shows "Starting..." and the game begins. On later launches, the
   remembered ROM and the existing module are reused, so the game starts right away.

## Controls

### Keyboard

| SNES | Key |
| --- | --- |
| D-pad | Arrow keys |
| B | Z |
| A | X |
| Y | A |
| X | S |
| L | Q |
| R | W |
| Start | Enter |
| Select | Right Shift |

**F11** switches fullscreen on and off. The window remembers its size, place and
fullscreen state between runs (in `~/.local/share/ct-recomp/hd2d/settings.ini`, or under
`~/.var/app/io.github.dittobyte.CtRecomp/data/` in the Flatpak, next to the saved look
and picture shape). A saved place is ignored if no screen shows it any more, and on
Wayland only the size and fullscreen state are kept, as the desktop picks the place.
`--scale` and Steam's Game Mode (always fullscreen) override it. `--help` lists the keys.

**Ctrl+Q**, or closing the window, quits. On a controller, hold **View + Menu** (Back +
Start) for 2 seconds: the game asks whether to quit, and **A** quits, **B** goes back. Esc does not quit: Steam Input sends Esc for a
controller button in some layouts.

### Controller

Any controller SDL recognizes as a game controller (Xbox, PlayStation, Switch Pro, Steam
Deck, ROG Ally and most others) works as pad 1. Buttons are mapped by position:

| SNES | Controller |
| --- | --- |
| B | bottom face button (Xbox A) |
| A | right face button (Xbox B) |
| Y | left face button (Xbox X) |
| X | top face button (Xbox Y) |
| L / R | left / right shoulder buttons |
| Start | Start / Menu |
| Select | Back / View / Share |
| D-pad | D-pad or left stick |

You can plug a controller in or out while the game runs. A Nintendo pad is read by
position too: its bottom button (Nintendo's B) is SNES B.

### Rumble

The SNES had no rumble, so the game makes it from what happens, on any controller with
motors (Xbox, PlayStation, Switch Pro, the Steam Deck, Steam's virtual pad under Steam
Input, and a ROG Ally on Bazzite outside Steam: [HANDHELD.md](HANDHELD.md#bazzite--inputplumber-rog-ally-legion-go--outside-steam)).
Short pulses only, and none in menus or text:

| What | How it feels |
| --- | --- |
| A party member is hit | a thump on that player's controller, stronger the more of their HP it took; harder when a hit knocks them out |
| Your hit lands on an enemy | a light tap; a critical hit sharper; a tech or magic heavier |
| An enemy goes down | a heavier thud after the hit |
| An earthquake (the field shaking in a scene) | a low rumble while it lasts |
| Leene's Bell, a time gate | a long low toll; a swell as you go through |
| The screen shaking or flashing (any scene) | a short pulse |

**Rumble** in the menu's **Controls** tab (`settings.ini` `rumble=`) is `off`, `light`
(the default), `medium` or `strong`; **Test rumble** under it plays a pulse. **Rumble from
low sounds** (`rumble_sounds=on`, off by default) adds a pulse for explosions and thuds in
the sound. In co-op each player feels their own party member's hits; the enemy's side
goes to whoever made the hit, and scene events to everyone. Rumble stops while the game is
paused, fast-forwarding or behind the menu, when you quit, and when a controller is
unplugged. It changes nothing in the game: a run with it on is the same, frame for frame,
as one with it off. `--rumble LEVEL` sets it for one run and `--log-rumble` prints every
pulse. The hits come from the built game code (as for always run): with `--interp-only`
only the screen's shaking and flashing rumble.

### Quick keys and back buttons

| Key | Back button (Steam Deck) | What it does |
| --- | --- | --- |
| F6 | L4 | Quick save |
| F9 | R4 | Quick load |
| Tab (hold) | L5 | Fast-forward while held |
| F1 | "..." (misc1) | The quick menu (below) |
| F10 | R5 | The menu (below); L3 + R3 together on any controller |
| F8 | | Screenshot |
| Backspace (hold) | | Rewind ([below](#rewind-slow-motion-and-frame-step)) |
| . (period) | | Frame step while paused |

What they do is described in [Quality of life](#quality-of-life); F1 / "..." open [the quick
menu](#the-quick-menu), F10 / R5 [the menu](#the-menu). A Steam Controller's grips are R4 and L4 (paddle1 and paddle2), an Xbox
Elite pad's paddles P1-P4 likewise. `settings.ini` changes them: `bind_paddle1=` ...
`bind_paddle4=`, `bind_misc1=`, `bind_touchpad=` take an action (`none`, `quicksave`,
`quickload`, `ffwd`, `menu`, `screenshot`, `framestep`, `rewind`, `quickmenu`), and `key_quicksave=F6` and the like take a
key name (`none` for no key). Details, and how this works with Steam Input:
[HANDHELD.md](HANDHELD.md#back-buttons-and-grips).

Messages on screen show the buttons of what you used last, a controller's (Steam Deck,
Steam Controller, Xbox, PlayStation, Switch) or the keyboard's, as small pictures.

### The quick menu

**F1**, the Deck's **"..." button** (misc1) or, on a touchscreen, **three fingers
tapped together** on empty spots, or a tap on the small **handle** at the top edge's
middle (it shows while you use the touchscreen and fades after a few seconds) opens the
quick menu: a sheet of big tiles over the paused picture, for what you want mid-play
without going through the settings. Like the menu, it makes the game wait: no frame
runs and the sound stops. Closing it (B, Start, F1, Escape, or a tap above the sheet)
carries on from the same frame.

| Section | Tiles |
| --- | --- |
| Game | Resume; Quick save; Quick load; Mute; Slot (pick 1-4), Save to slot, Load slot; Volume (a slider) |
| Look | Look (cycles the HD-2D look, as R3 does; HD-2D only); Aspect; Integer scale; CRT look; Fullscreen |
| More | All settings... opens the F10 menu |

Tiles are at least 72 px at 1280x800 and follow the menu text size setting; there are
four columns on a wide screen, three on a tall one. A tile is a setting the F10 menu
has too, so the two always agree and `settings.ini` keeps them.

| | Controller | Keyboard | Touch and mouse |
| --- | --- | --- | --- |
| Move the focus ring | D-pad or left stick | Arrow keys | Tap a tile |
| Run, toggle, step | A | Enter or Space | Tap |
| Adjust a slider or stepper | Left / right | Left / right | Drag the slider, tap an arrow |
| Next / previous section | R1 / L1 | W / Q, Page Down / Page Up | Drag the sheet to scroll |
| Close | B, Start, or the open button again | Escape, Backspace or F1 | Tap above the sheet |

The line at the bottom shows the buttons of what you used last. F1 used to cycle the
look: that is the Look tile now, and R3 still cycles it.

### The menu

**F10**, the Deck's **R5**, or **L3 + R3** (both sticks clicked together, on any
controller) opens the settings menu over the picture. The game waits while it is open:
no frame runs and the sound stops, so it picks up exactly where it was. Every change is
saved at once and, where it can be, applied at once (the look, the camera, the volume
and co-op change while you watch; the renderer and the Steam Controller driver on the
next start).

| | Controller | Keyboard |
| --- | --- | --- |
| Move | D-pad or left stick (hold to repeat) | Arrow keys |
| Change a value | Left / right, or A | Left / right, or Enter |
| Run, toggle | A | Enter or Space |
| Back, close the menu | B (or Start) | Escape or Backspace (or F10) |
| Switch tabs | L1 / R1 | Q / W, or Page Up / Page Down |

The mouse works too: click a row, click the left or right half of its value to step it,
the wheel scrolls. The bottom of the menu shows the buttons of what you used last.
Closing the menu with a button doesn't press it in the game.

| Tab | What is in it |
| --- | --- |
| Look | The renderer (original or HD-2D, from the next start), the HD text font and its style and size, the HD-2D look, the picture shape, the 3D diorama's camera tilt and zoom, its render scale (AMD FSR 1), sharpening and ambient occlusion, gyro aiming and its strength, the debug view |
| Display | Fullscreen, frame pacing (timer or vsync), the menu's text size, integer scale and the CRT look (original picture), the battery mode |
| Controls | What each back button does, the quick keys (pick a row, then press the key), the Steam Controller driver, fast-forward (on until turned off) and its speed, always run, turbo per button, pausing in the background or when a controller goes, rumble and **Test rumble** |
| Co-op | Co-op auto / on / off, which player plays which party member, the experimental split screen |
| Packs | The asset packs found in your packs folder: off, or on at a place in the order; reload; dump new art |
| Save states | Load the quick slot, slots 1-4 or the resume point (with a picture of each; loading closes the menu), save to them (press twice), resume on start, take a screenshot |
| Audio | The volume, muting while another window has the focus, the sample interpolation (Gaussian like the console, or the crisper cubic or sinc) |
| About | The version, the licenses of everything in it, the bring-your-own-ROM note, and Quit (press twice) |

Rows that don't apply right now are grey (the HD-2D settings in the original picture,
fullscreen in Game Mode). Its settings are in `settings.ini` too, as `renderer=hd2d` or
`plain`, `cam_tilt=40`, `cam_zoom=100`, `render_scale=auto`, `sharpen=auto`, `ao=auto`,
`ao_size=auto` ([HD2D.md](HD2D.md#render-scale-sharpening-and-ambient-occlusion)),
`pacing=auto`, `menu_scale=normal`, `volume=100`, `mute_unfocused=off`,
`audio_interp=gaussian` (or `cubic`, `sinc`), `hd_font=FILE`, `hd_text_style=shadow` (or
`outline`, `plain`) and `hd_text_size=100`; a `--hd2d`, `--no-hd2d` or `--pacing` at
launch still wins.
The mouse cursor hides after 2 seconds without moving.

### The HD-2D look

Start with `--hd2d` to draw the game as a lit diorama, with depth of field, bloom, soft
shadows and a color grade. Every pixel still comes from the game's own picture: the
renderer adds lighting and blur, not new art. More detail is in [HD2D.md](HD2D.md).

| Key | Controller | What it does |
| --- | --- | --- |
| | R3 (click and let go of the right stick) | Cycle the look: original, subtle, HD-2D (the default), HD-2D Steam Deck, 3D diorama (also the Look tile of the [quick menu](#the-quick-menu), F1) |
| F2 | | Cycle the debug views: layers, depth, blur, side-by-side compare, off |
| F3 | L3 (click and let go of the left stick) | Cycle the picture shape: 4:3 (default), pixel 8:7, stretch |
| F11 | | Toggle fullscreen |

F11 works without `--hd2d` too. F2, F3, R3 and L3 only do something with `--hd2d`. Each
change shows a short message on screen, and the look and the picture shape are
remembered for next time.

### Two players (co-op)

A second controller is player 2. As soon as two controllers are connected, co-op is
on (**F4** turns it on or off; `--coop` and `--no-coop` at launch). The choice is saved in
`settings.ini` as `coop=auto`, `on` or `off`. Player 1 keeps the keyboard and the first
controller.

- **Battle:** when the command menu of party member 2 or 3 opens, player 2's controller
  picks the command and the target. Player 1's buttons do nothing until it closes, and
  a button player 2 is already holding when the menu opens counts only once it is let
  go. `coop_chars=2,3` in `settings.ini` lists the party members (1-3) player 2 plays.
  With a third controller, `coop_chars3=3` gives party member 3 to player 3 (pad 3).
  If a controller is unplugged, its characters go back to player 1 at once, and the
  battle doesn't wait.
  - *One character each:* set `coop_chars=2` and `coop_chars3=3`. Player 2 then plays
    party member 2 and player 3 plays member 3. With only two controllers, player 1
    keeps member 3.
  - While a menu belongs to another player, a tag in its colour (**P2** or **P3**) shows
    by the command windows. In HD-2D, a short message names whose turn it is.
  - Left and right in the command menu move to the next ready party member, but each
    player only moves among their own. With none of yours ready, the menu stays where
    it is.
  - Only the battle menus are shared. Everything else (the field, the X menu, shops,
    dialogue) takes pad 1, and so does the soft reset (L + R + Select + Start, held
    together). Player 2 can't reset the game.
  - Characters are assigned by party slot, not by who they are. If the party changes,
    player 2 plays whoever is now in their slots.
- **Field:** player 2 walks party member 2 with the d-pad (hold **B** to run). Walls stop
  them. When player 2 lets go, the character walks back into line behind the leader
  and follows as usual. It snaps back into line if it gets stuck, wanders more than a
  screen away, or when a cutscene starts. After a battle it rejoins the same way, and
  after a door or map change it starts where the game puts it. Only the leader opens
  doors, takes exits, talks to people, and starts events and battles.
- **Split screen (experimental):** with `--coop-split`, when player 2's character is more than
  about 3/4 of a screen from the leader, the window splits in two. On a wide window the
  views sit side by side; otherwise one is above the other. The second view follows
  player 2's character, and the views merge again when the two are close. It needs the
  plain picture (`--no-hd2d`), and the leash is two screens. People near player 2 are
  drawn too, but this changes how the game handles its sprites. A busy map can run out
  of the game's sprite slots, so some people may not show in either view. The
  background in the second view has no parallax.

Co-op needs the game module (the normal setup). With `--interp-only` the game plays as
one player.

### Asset packs

An asset pack replaces the game's tiles and sprites with pictures at a higher
resolution. You make one yourself from your own copy of the game: play with
`--pack-dump mydump` to save the pictures the game shows, upscale them with
`python3 tools/pack_upscale.py mydump --scale 4 --out my4x` (in the source tree;
packages install it as `share/ct-recomp/tools/pack_upscale.py`), and play with
`--pack my4x`. Edit any of its PNGs and press **F5** to see the change. The whole
workflow, the format and the rules are in [ASSET-PACKS.md](ASSET-PACKS.md).

**A pack made by dumping is the game's own art, and an upscaled or edited copy of it
still is. It is for your personal use only: never share or upload it.** Pictures you
paint yourself from scratch are yours.

Besides single tiles, a pack can hold:

- **Poses:** a character's whole animation frame as one picture, at any size. Play with
  `--pack-export-sheets mysheets` to get each character's poses on a sprite sheet
  (`ct/sheets/mysheets/sheet_pc00_4x.png` and its index), paint over it in Aseprite,
  Krita or an image-to-image tool, and cut it back into a pack with
  `python3 tools/pack_import_sheet.py EDITED.png --index sheet_pc00.json --pack mychars`.
- **3D models:** glTF files (from Blender, Sprytile or Crocotile 3D) placed on map cells,
  drawn in the 3D diorama look (`hd2d3d`), lit and shadowed like the map. Run with
  `CT_PACK_WHERE=1` to see the map id and the cell you stand on.

The formats, the axes and units for models, and the sheet workflow are in
[ASSET-PACKS.md](ASSET-PACKS.md). **Sheets are the game's art too, and so is anything
painted over them: for your personal use only.** Models you build from scratch, with
your own textures, are yours.

Packs need the GL renderer. Without `--hd2d` the game switches to it with the original
look while a pack is loaded.

| Key | What it does |
| --- | --- |
| F5 | Reload the asset packs (after editing their pictures or their music) |

### Audio packs

A pack can also replace the game's songs with sound files of your own (Ogg Vorbis, FLAC
or WAV): add a `[[music]]` entry per song to its `pack.toml`, with the game's song id
and the file. Play once with `--music-log` to see the ids as the songs start. While a
replaced song plays, the console's music is muted and the sound effects stay. Songs
loop and fade as the game asks. How to make one, and the limits (no echo on the sound
effects while a song is replaced), are in
[ASSET-PACKS.md](ASSET-PACKS.md#music-audio-packs).

**An audio pack made from the game's music (a recording or rip of the soundtrack, or an
arrangement of it) is for your personal use only: never share or upload it, or any file
in it.** Music you made yourself is yours.

Audio packs work with the original picture too, also with `--no-hd2d`.

### HD text

The dialogue boxes' text can be drawn with a font of your choice at your screen's full
resolution, instead of the console's 8-pixel letters. The project comes with no font:
pick any TrueType or OpenType font you have, or download a free one, for example
[Noto Sans](https://fonts.google.com/noto/specimen/Noto+Sans),
[Atkinson Hyperlegible](https://fonts.google.com/specimen/Atkinson+Hyperlegible),
[Fira Sans](https://fonts.google.com/specimen/Fira+Sans) or
[Inter](https://fonts.google.com/specimen/Inter) (all under the SIL Open Font License).
A bold weight reads best.

1. Put the `.ttf` or `.otf` file in the `ct/fonts/` folder of your data folder
   ([Where your files live](#where-your-files-live)), for example
   `~/.local/share/ct-recomp/ct/fonts/NotoSans-Bold.ttf`.
2. Start once with `--hd-font NotoSans-Bold.ttf` (a name without a folder is looked for
   in `ct/fonts/`; a path is used as it is), or write `hd_font=NotoSans-Bold.ttf` in
   `settings.ini`. It is remembered.
3. In the menu's **Look** tab, **HD text style** picks a drop shadow like the console's,
   an outline or neither, and **HD text size** scales the letters (50-150%).

Each word starts where the console starts it, so the lines break where the game breaks
them. The text keeps the game's colors and fades with the screen. A character your font
doesn't have (the game's music note or heart, say) is drawn by the console as before.
`hd_font=none` (or deleting the line) turns HD text off.

HD text needs the GL renderer: without `--hd2d` the game switches to it with the original
look, as for asset packs; with `--no-hd2d` there is no HD text. It follows the game
module (with `--interp-only` there is none). Menus, battle messages and the name entry
still use the console's font: only the dialogue boxes are done so far.
| F5 | Reload the asset packs (after editing their pictures or models) |

## Quality of life

### Save states

A save state is the whole console at one moment, separate from the game's own save slots.

| Key | Deck / Ally | What it does |
| --- | --- | --- |
| F6 | L4 | Quick save |
| F9 | R4 | Quick load |
| Shift+F6 / Shift+F9 | | Save to / load the selected slot (1-4) |
| F7 | | Select the next slot; the message says whether it is empty |

Quick save, quick load, fast-forward and the screenshot are the frontend's actions: their
keys and back buttons can be changed in `settings.ini` (see [Quick keys and back
buttons](#quick-keys-and-back-buttons)). Shift with whatever key quick save or quick load
is on works with the selected slot. The other keys here are fixed for now, and never
take a key an action has.

States are kept in `ct/states/` in your user data folder (see [Where your files
live](#where-your-files-live)): `quick.state`, `slot1.state` to `slot4.state` and
`auto.state` (the resume point), each with a `.png` picture of the screen at that moment.
They are your files, like your saves: they are never part of the project and never
shared. A state is about 600 KB.

- A state is taken at the end of the frame you press the key in, wherever the game is
  (field, world map, battle, menus).
- Loading a state does not change your in-game saves. The cartridge's save memory stays as
  it is on disk, so loading an old state never undoes a save you made in the game.
- A state only loads in the same engine version and with the same ROM. Another one is
  refused with a message; nothing changes.
- While `--record` is recording, loading is refused: the recording couldn't replay it.

### Resume where you left off

When you quit (Ctrl+Q, View+Menu, closing the window, or Steam's Exit Game, which stops
the program with SIGTERM), the engine saves a resume point. It also saves one when the
system goes to sleep. The next start shows **Resume? [A] yes [B] no** (with your
controller's buttons): **A** (or Enter)
carries on from there, **B** (or Esc) starts the game from the beginning and keeps the
old resume point as `auto-previous.state`, in case you pressed it by mistake.

Nothing is saved if less than a second was played, so quitting at the question keeps the
resume point you had. `--no-resume` skips the question for one start, `resume=off` in
`settings.ini` turns the feature off. `--no-save` also turns it off.

Sleep is noticed through logind (the `PrepareForSleep` signal on the system D-Bus). The
engine holds a "delay" lock so the system waits for the resume point to be written, at
most a few seconds. The sound stops before sleep and starts again cleanly afterwards,
and the frame timer starts over, so the game doesn't race to catch up. Without D-Bus
(some sandboxes) only the wake-up is noticed, and the resume point is saved on quit.

### Fast-forward and turbo

| Key | Deck / Ally | What it does |
| --- | --- | --- |
| Tab (hold) | L5 (hold) | Fast-forward while held |
| ` (grave) | | Fast-forward on / off |
| Ctrl+T | | Turbo on / off for the buttons you are holding (none held: all off) |

Fast-forward runs the game at `ffwd_speed` times the normal speed (2, 3 or 4; default 3)
and shows one frame in that many. The sound is muted while it runs: sped-up music is not
pleasant, and pitch-correcting it would cost time on a handheld. It comes back at the
normal speed.

Turbo presses a button 15 times a second while you hold it (two frames down, two up). It
works for A, B, X, Y, L and R, on any pad, and is saved as `turbo=` in `settings.ini`.
It is off by default.

### Pause

The game pauses, with **Paused** on screen, when:
- a controller is disconnected (`pause_on_disconnect`, on by default): press a button on
  any controller, or a key, to go on;
- the window loses focus (`pause_on_focus_loss`, `auto` by default: on, but off in
  Steam's Game Mode, where gamescope moves the focus around on its own); it goes on when
  the window has the focus again;
- you press **Pause** or **P**.

Sound stops while paused, and the frame timer starts over after.

### Rewind, slow motion and frame step

| Key | What it does |
| --- | --- |
| Backspace (hold) | Rewind: the game runs backwards, about twice as fast as it went; let go to play on from there |
| . (period) | Frame step: while paused, run exactly one frame (not paused: it pauses first) |

The two have no controller button by default (the back buttons are taken); bind one with
`bind_touchpad=rewind`, `bind_misc1=framestep` and the like, or change the keys with
`key_rewind=` and `key_framestep=`. The quick menu's Time tiles do the same.

- **Rewind** keeps a snapshot of the whole machine every 4 frames (about 15 a second),
  as many as `rewind_seconds` allows (default 5, `0` turns it off), compressed against the
  one before and never more than about 32 MB. Holding the key steps back through them,
  sound off. The cartridge's own save is left as it is, as for a quick load: rewinding
  never undoes an in-game save. It stops at the oldest snapshot while you hold on, and it
  does not work while `--record` writes a script (a rewound frame would be recorded twice;
  it says so on screen). Snapshots cost about 0.3 ms of CPU every 4 frames on a desktop;
  turn `rewind_seconds` to 0 on a very slow device.
- **Slow motion**: `slow_speed` is 25, 50, 100, 200, 300 or 400 percent of normal
  (menu: Controls, Game speed; the quick menu steps through them). The sound is off
  whenever it is not 100, as in fast-forward. Fast-forward multiplies it: 50 with
  fast-forward at 3x runs at 150 percent.
- **Frame step** advances one frame per press while paused (**P**), with the buttons you are
  holding, and no sound.

### Battery

`battery_mode` in `settings.ini` (default `off`) uses less power on battery. The engine
checks `/sys/class/power_supply` every few seconds: on battery means a battery is
discharging and no charger (mains, USB or USB-C) is connected.

| `battery_mode` | On battery |
| --- | --- |
| `off` | Nothing changes |
| `light` | The HD-2D look switches to the lighter **deck** preset |
| `saver` | As `light`, and the picture is drawn at 30 Hz. The game still runs at 60, so its speed and timing don't change |

Back on the charger, your own look comes back. The look saved in `settings.ini` is always
yours, not the battery's.

### Screenshots

**F8** (or Print Screen; no back button by default) saves exactly what is on screen, at the window's full size (the
HD-2D picture when it is on), as a PNG in `$XDG_PICTURES_DIR/ct-recomp/`, usually
`~/Pictures/ct-recomp/`. A message gives the file name. In Game Mode, Steam's own
screenshot (Steam + R1) works too.

### Always run

**Caps Lock** (or `always_run=on`) makes the party run in the field without holding the
run button; holding it walks. It changes how the game's walking code reads the button,
not the button itself, so menus and anything else that checks it behave as usual. It
needs the built game code: in the interpreter (`--interp-only`) it does nothing.

### The original picture: integer scale and CRT

Without HD-2D, the picture is scaled by the largest whole number that fits the window,
with black bars around it, so every console pixel is the same size. `integer_scale=off`
fills the window's height (or width) instead, keeping the shape.

**Ctrl+R** (or `crt=on`) adds a CRT look to that picture: dark gaps between the lines, a
faint red-green-blue stripe mask and a soft glow around bright parts. It is drawn with the
renderer's blending, so it costs next to nothing and works with any graphics driver; the
glow needs a GPU renderer and is left out on the software one. It doesn't apply to the
HD-2D picture.

### Saves from other emulators

```
ct-recomp --import-srm "Chrono Trigger.srm"
ct-recomp --export-srm ~/ct-backup.srm
```

The file must be the plain 8,192-byte save memory that SNES emulators (snes9x, bsnes,
Mesen, RetroArch's cores) write as `.srm`. Anything else is refused. An import keeps the
save it replaces as `ct.srm.before-import-DATE-TIME` and writes the new one the same safe
way as the game's saves. Both stop after the copy, without starting the game. They work
on the `--save` file when you give one.

### Settings

All in the [menu](#the-menu) (Display, Controls and Save states tabs) and in
`settings.ini`, with their defaults:

| Setting | Default | |
| --- | --- | --- |
| `ffwd_speed` | `3` | 2, 3 or 4 |
| `turbo` | (none) | buttons with turbo, e.g. `a,b` |
| `pause_on_focus_loss` | `auto` | `on`, `off`; `auto` is off in Game Mode |
| `pause_on_disconnect` | `on` | |
| `resume` | `on` | the resume point and the question |
| `battery_mode` | `off` | `light`, `saver` |
| `crt` | `off` | the original picture only |
| `integer_scale` | `on` | the original picture only |
| `always_run` | `off` | |
| `slow_speed` | `100` | 25, 50, 100, 200, 300 or 400 percent |
| `rewind_seconds` | `5` | 0 to 30; 0 is off |
| `rumble` | `light` | `off`, `medium`, `strong` ([Rumble](#rumble)) |
| `rumble_sounds` | `off` | pulses for low booms in the sound too |

`--qol-set KEY=VALUE` sets one for a single run; `--states-dir DIR` keeps the states
somewhere else; `--load-state FILE` starts from a state.

## Where your files live

The engine keeps its files in your user data folder, `~/.local/share/ct-recomp/` (or
`$XDG_DATA_HOME/ct-recomp/` if you set `XDG_DATA_HOME`). In the Flatpak, that folder is
`~/.var/app/io.github.dittobyte.CtRecomp/data/ct-recomp/`.

| File | What it is |
| --- | --- |
| `ct/ct_game.so` | The game module built from your ROM. Don't share it. |
| `ct/ct_game.stamp` | The ROM checksum and engine version the module was built for |
| `ct/rom_path` | Where your ROM is, so you don't have to pick it again |
| `ct/ct.srm` | Your in-game saves (see [Your saves](#your-saves)) |
| `ct/ct.srm.bak` | The save file as it was before the last write |
| `hd2d/settings.ini` | Everything the menu sets: the look and picture shape, the window, co-op, the buttons and keys, the volume, the asset packs to load, the [quality-of-life settings](#settings) |
| `ct/states/` | Save states and the resume point, each with a `.png` of the screen (see [Save states](#save-states)) |
| `ct/packs/NAME/` | Asset pack NAME: `pack.toml` and its pictures, and its music (`[[music]]` files). Made from the game's art or music unless you made every picture and song yourself: **never share it.** |
| `ct/fonts/` | Fonts for [HD text](#hd-text), which you put there (none come with the game) |
| `ct/packs/NAME/` | Asset pack NAME: `pack.toml` and its pictures. Made from the game's art unless you painted every picture yourself: **never share it.** |
| `ct/sheets/NAME/` | Sprite sheets from `--pack-export-sheets`: the game's own frames, **never share them** |
| `ct/gen/`, `ct/obj/` | Temporary build files, normally deleted after the build. If a build fails they can be left behind: they hold code generated from your ROM, so don't share them. Deleting them is safe. |

The ROM itself is never copied: only its location is saved.

### Your saves

The cartridge's battery-backed save memory (8 KiB, which holds the game's save slots) is
kept in `ct/ct.srm`, as a plain 8,192-byte copy of that memory. It is loaded at start-up
and written back a few seconds after the game changes it, for example right after you
save in the game, and again when you quit, also when you close the window or the
program stops with an error. Each write goes to a temporary file first and then replaces
`ct.srm`, so a crash or a power cut leaves either the old save or the new one, never half
of each. The save before the last write is kept as `ct.srm.bak`. To go back to it, quit
and copy `ct.srm.bak` over `ct.srm`.

A `ct.srm` that isn't exactly 8,192 bytes is never used or overwritten: the engine
renames it to `ct.srm.bad`, prints a warning, and starts with empty save memory.

To keep the saves somewhere else, start with `--save /path/to/file.srm`. Save states are
separate from these saves (see [Save states](#save-states)), and saves from other
emulators can be brought in with `--import-srm` (see [Saves from other
emulators](#saves-from-other-emulators)).

## Troubleshooting

Messages in the window are kept short. For details, start the program from a terminal
and read its output. The program's lines start with `ct_sdl:`, and the `ct_sdl: exit:`
line at the end gives the reason it stopped.

### "... is not the expected ROM" or "... is N bytes, expected 4194304"

The file isn't the dump the port needs (see [What you need](#what-you-need)).
- **"It has a 512-byte copier header: remove it."** Remove the first 512 bytes, as shown
  above, and drop the new file on the window.
- **A different size, or a different SHA-1:** it's another region, revision or a modified
  ROM. You need the unmodified US 1.0 dump.
- **"Not a file" or "Cannot read":** check the path. In the Flatpak, check that the file
  is in Downloads or Documents, or grant access with `flatpak override`.

### "The game code could not be built."

The window shows the last lines of the build's output. Press **A** (or Enter) to try the build
again, or **B** (or any other key) to **play anyway in the interpreter**: the game runs without the module, with every instruction
interpreted. It's slower: on a ROG Ally it measured about 290 frames per second without
a window, against the 60 the game needs, so it's still playable on most PCs. The build is tried again on the next launch.

Common causes:
- **Out of disk space:** free some space in your home folder.
- **"cannot start the build tool (is python3 installed?)":** the package's Python is
  missing. Reinstall the AppImage or the Flatpak.
- **Something else:** run the program from a terminal and keep its output for a bug
  report.

"The game code would not load." means a module was built but couldn't be loaded. (A
module left over from an earlier run is rebuilt once before this message.) Again, any
A tries again, and B or any other key plays in the interpreter.

### Rebuilding the module

The engine rebuilds the module by itself when your ROM or the engine's module format
changes. To force a rebuild, for example after an update that seems not to have taken
effect, quit and delete the stamp file (or the module itself):

```
rm ~/.local/share/ct-recomp/ct/ct_game.stamp
# Flatpak:
rm ~/.var/app/io.github.dittobyte.CtRecomp/data/ct-recomp/ct/ct_game.stamp
```

The next launch builds it again. To use a different ROM file, start once with
`--rom /path/to/file`, or delete `ct/rom_path` to be asked again.

### Graphics and drivers

The HD-2D look needs **OpenGL 3.3**, or OpenGL ES 3.0. If it isn't available, the
terminal shows `no usable OpenGL 3.3 / ES 3.0 context` and the program exits. Start
without `--hd2d` to play with the original look, which works on any graphics driver.

**Steam Deck, ROG Ally and other handhelds.** Add the AppImage to Steam as a non-Steam
game; for the Flatpak, add it with Steam's "Add a Non-Steam Game" and pick
**CT Recomp**. The game starts with the original look; for HD-2D, turn it on in the menu
(Look tab, renderer; it is remembered) or add `--hd2d` to the shortcut's launch options
(for the Flatpak shortcut, at the end of the existing launch options).
- In Game Mode (gamescope) the game starts fullscreen, with no mouse
  cursor. The full guide is [HANDHELD.md](HANDHELD.md).
- Use R3 and L3 for the look and the picture shape, since handhelds lack F-keys.
- Use a Steam Input layout that sends gamepad buttons, not a keyboard or desktop layout.
- The renderer takes about 2 ms per frame on a ROG Ally at roughly 1080p's pixel count,
  so a lower TDP is usually fine. `--log-gpu` prints the frame time if you want to check.

**NVIDIA (proprietary driver).**
- Flatpak: the GL driver comes from the `org.freedesktop.Platform.GL.nvidia-*` extension,
  which must match your installed driver version. After a driver update, run
  `flatpak update` so they match again. A mismatch shows up as the OpenGL error above.
- If the window doesn't open on a Wayland desktop, try X11 through XWayland:
  `SDL_VIDEODRIVER=x11 ./CT_Recomp-x86_64.AppImage --hd2d` (for the Flatpak,
  `flatpak run --env=SDL_VIDEODRIVER=x11 io.github.dittobyte.CtRecomp --hd2d`).

- The HD-2D code was checked against a strict GL 3.3 core profile on Mesa
  (radeonsi and llvmpipe, with every GL error reported), but never on NVIDIA's driver.
  If you see a black or garbled picture there, run with `CT_HD2D_GLERR=1` set and
  send the terminal's `hd2d: GL error` lines.

**AMD and Intel (Mesa).** Use an up-to-date Mesa from your distribution. Old Intel GPUs
without OpenGL 3.3 can still play without `--hd2d`.

### No sound, or no controller

- **Sound:** the program uses your desktop's sound server (PulseAudio or PipeWire). Check
  that the output device is right in your system's sound settings.
- **Controller:** run from a terminal. At startup the program lists every joystick it
  sees, and marks the ones it can't use as `(joystick only: not used)`. Only
  controllers that SDL recognizes as game controllers work. Each one is listed with its
  USB vendor:product and the button pictures it gets.
- **Steam Deck or Steam Controller outside Steam:** the game reads them with SDL's own
  drivers, which need access to `/dev/hidraw*` (Steam's udev rules, the `steam-devices`
  package on some distributions). `steam_hidapi=off` in `settings.ini` leaves the Steam
  Controller to other drivers ([HANDHELD.md](HANDHELD.md#without-steam-input-desktop-mode)).
- **ROG Ally, Legion Go and the like on Bazzite, outside Steam:** InputPlumber shows the
  built-in controls as an emulated Steam Deck controller that SDL doesn't recognize; the
  game reads it itself (`hid_deck=auto|on|off` in `settings.ini`, the menu's **Controls**
  tab; [HANDHELD.md](HANDHELD.md#bazzite--inputplumber-rog-ally-legion-go--outside-steam)).
