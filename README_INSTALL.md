# Install guide

Українською: [README_INSTALL.uk.md](README_INSTALL.uk.md). Project overview: [README.md](README.md).

**This repository holds no game data.** You need your own disc of Castlevania: Symphony of the Night
(PlayStation, USA, SLUS-00067). The build reads your disc image and makes the game files on your machine.
Do not ask for disc images or game data. Do not share them.

A line marked `[unverified]` was not run end to end. The other steps use the same scripts and commands that made the owner's Windows play folder (build 0.8.113).

## 1. What you need

| Item | Detail |
|---|---|
| Your disc image | PS1 USA, SLUS-00067: a `.cue` file and its `.bin` files (a dump of your own disc; tracks 1 and 2 are normal). Required. |
| Optional discs | PSP "The Dracula X Chronicles", Europe, ULES-00841 (`.iso` or `.cso`): Maria. Sega Saturn, Japan, T-9527G (`.bin` and `.cue`): Saturn stages. |
| A Linux build host | Ubuntu 24.04 or Debian. On Windows this is WSL 2 (see step 2). `[unverified]` The free disk space for the build was not measured. |
| Windows | 64-bit Windows 10 or 11, OpenGL 3.3 video driver. `[unverified]` the lowest Windows version. |
| Switch | A console with Atmosphère and hbmenu, an SD card. `[unverified]` the lowest firmware and Atmosphère versions. |

CHD, PBP, 7z, ZIP, RAR and ECM images are not read. Convert them to `.bin` and `.cue` first
(for CHD: `sudo apt install mame-tools; chdman extractcd -i game.chd -o game.cue`).

## 2. Make the game files (all platforms)

The game files (`data/`) come from your disc. The build script does it for you.

Windows only, once, in an administrator PowerShell, then restart Windows and open "Ubuntu" once to make a user:

```powershell
wsl --install -d Ubuntu-24.04
```

Then, in Ubuntu (WSL) or on Linux:

```sh
git clone -c core.autocrlf=false --recurse-submodules -b switch https://github.com/rashevskyv/sotn-native.git ~/sotn-native
cd ~/sotn-native
# put your disc images into games/ (any file names). Windows drive C: is /mnt/c/
cp "/mnt/c/Users/<you>/Downloads/Castlevania SOTN (USA) (Track 1).bin" games/
cp "/mnt/c/Users/<you>/Downloads/Castlevania SOTN (USA).cue" games/
# optional: more tracks, the PSP .iso, the Saturn .bin and .cue go into games/ too
python3 build.py --install-deps
```

- The clone must live in the WSL file system (`~/`), not on `C:` or `D:` (the build is much slower there).
- `--install-deps` installs the missing tools with `sudo` and needs the network (apt, Go modules, Rust crates).
  Without it the build prints the install commands and stops.
- The script finds each disc by its content, not by its file name. It prints what it found.
- Options: `--pc-only` (no Switch build, no devkitPro needed), `--switch-only`, `--clean`.
- The log is `out/build.log`. A second run skips the steps that are done.

What the build makes:

| Folder | What |
|---|---|
| `out/pc/` | The Linux PC build with its `data/` folder, `sotn-xa.cue` and `run.sh`. Start it with `out/pc/run.sh`. On Windows, `out\pc\run.bat` starts it in WSL. |
| `out/sd/` | The Switch build: `switch/sotn-native/sotn.nro` with its `data/`. |

The `data/` folder in both is the same game data: the files of your disc, the HD pictures of this repository
(see step 6) and, with the Saturn disc, the stitched Saturn rooms.
Inside the build the disc files are made by `bin/sotn-disk extract <sotn.cue> disks/us`
(`make bin/sotn-disk` builds the tool; Go 1.24 is needed). `[unverified]` this tool was not run again for this guide.

## 3. Windows

### 3a. Build `sotn.exe`

`build.py` makes the Linux build. The native Windows program is a MinGW cross build.
Run it in WSL, in the same clone, after step 2 (it needs `disks/us` and `assets/` that `build.py` made):

```sh
sudo apt install mingw-w64
cd ~/sotn-native
cmake -S . -B build/win -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=tools/psyz/psyz/cmake/windows-x86_64.cmake -DCMAKE_BUILD_TYPE=Release \
      -DENABLE_ASAN=OFF -DPSYZ_RENDERER=sdl3_gl -DPSYZ_STATIC_MODULES=ON -DSOTN_PC_DATA_DIR=data \
      -DENABLE_SATURN_STAGES=ON
cmake --build build/win
```

- Leave out `-DENABLE_SATURN_STAGES=ON` if you have no Saturn disc.
- Maria turns on by itself when you gave the PSP image.
- Optional: `-DSOTN_ICON=<file.ico>` for the program icon.
- Result: `build/win/sotn.exe` (one file, no DLL, no console window) and `build/win/data/` (about 670 MB with the HD pictures).

### 3b. Make the Windows folder

```sh
mkdir -p /mnt/c/Games/sotn-native
cp build/win/sotn.exe /mnt/c/Games/sotn-native/
cp -r build/win/data /mnt/c/Games/sotn-native/
cp out/pc/sotn-xa.cue /mnt/c/Games/sotn-native/
# only with the Saturn disc: the stitched rooms replace the files in data/ST
cp -r build/win/saturn_stitches/disks/us/ST/. /mnt/c/Games/sotn-native/data/ST/
```

Start `C:\Games\sotn-native\sotn.exe`. `sotn.ini` is made at the first start with the default settings.

```text
C:\Games\sotn-native\
|-- sotn.exe          the program
|-- sotn.ini          settings; the game makes it, the port menu writes it
|-- sotn-xa.cue       3 lines that point at data\SD\XA_STR1 (disc music)
|-- data\             game data from your disc
|   `-- BIN\ BOSS\ SD\ SERVANT\ ST\ VAB\ XA\ and the disc files (DRA.BIN, ...)
|-- music\            optional, enhanced music (step 5)
|-- voice\            optional, voice sets jp\ psp\ (step 5)
|-- bu00\ bu10\       memory card saves (the game makes them)
`-- sotn.log, sotn.prev.log, sotn.out.log
```

If the game finds no `data\BIN\F_TITLE0.BIN` it shows the message "Game data not found" and stops.
That file is the only one the start check needs. `sotn-xa.cue`, `music\` and `voice\` are optional.
The build embeds the HD packs in the program. It makes no `data\textures\` folder.
Without `sotn-xa.cue` the game plays no disc music. The text of the cue file:

```text
FILE "data/SD/XA_STR1" BINARY
  TRACK 01 MODE2/2352
    INDEX 01 00:00:00
```

Both `data/SD/XA_STR1` and `data/SD/XA_STR1.` work in the cue. The port opens the name that exists.

## 4. Nintendo Switch

Copy the content of `out/sd/` to the root of the SD card (from WSL: `cp -r out/sd/. /mnt/<drive>/`, or use Explorer on `\\wsl.localhost\Ubuntu-24.04\home\<you>\sotn-native\out\sd\`).
By hand, the result must be:

```text
/switch/sotn-native/
|-- sotn.nro          the program. The file name must be exactly sotn.nro
|-- data/             game data from your disc, with data/textures/{en,uk,common}/
|-- music/            optional (step 5)
|-- voice/            optional (step 5)
|-- sotn.ini          the game makes it at the first start
|-- sotn-xa.cue       the game writes it at every start
|-- bu00/ bu10/       memory card saves
`-- sotn.log, sotn.prev.log, sotn.out.log
```

1. Put the SD card in the console. Start Atmosphère.
2. Open hbmenu and start "Castlevania: SOTN". `[unverified]` Start hbmenu through a game title (full memory). The album applet mode was not tested.
3. EXIT GAME in the port menu goes back to hbmenu.

The port uses its own folder and does not touch `/switch/sotn/`. Do not copy a `sotn.ini` from another install.
If the game goes back to hbmenu at once, read `/switch/sotn-native/sotn.log` and find the line `[boot] data dir`.
`[unverified]` The free SD space that the game needs. `data/` is about 670 MB (Windows build with the Saturn stages).

## 5. Optional: music and voice

**Music (`music/`).** The disc music always works. "Enhanced" music replaces it with your own files.
The folder holds `<track id>.mp3` or `.flac` (44100 Hz) and an optional `loops.txt`.
The tool `tools/music/ost_map.py` makes the folder from the official soundtrack album (KICA-7760) that you own:

```sh
python3 -I tools/music/ost_map.py copy <folder with the 34 "NN Title.mp3" files> <out>/music
```

Copy that `music` folder next to `sotn.exe` (or into `/switch/sotn-native/`), then pick MUSIC = ENHANCED in the port menu.
No audio is in the repository.

**Voice (`voice/jp/`, `voice/psp/`).** These are clips `<ID>.pcm` (raw signed 16-bit, mono, 37800 Hz) for the voice sets
Japanese and PSP English. `build.py` makes them from your PSP image with `tools/voicepack/extract_voice.py`, but it needs
the files `manifest_voice_jp.json` and `manifest_voice_pspen.json` in `games/`. They are not in the repository.
`[unverified]` Without them the voice sets jp and psp play the PS1 English voice. The game works.
When `out/pc/voice/` exists, copy it next to the program.

## 6. HD texture packs and language

The HD pictures are part of this repository: `tools/lang/textures/{uk,en,common}` (PNG and packed `.hdt`).
The build copies them to `data/textures/<language>/` by itself. There is nothing to download.

- `uk/` pictures show with the Ukrainian language, `en/` with English. `common/` pictures show with both.
- Switch them off with `hd_textures = 0` in `sotn.ini` (or the port menu, DISPLAY, HD TEXTURES).
- Without the folder the game runs with the built-in pictures. Details: [docs/ASSET_SOURCES.md](docs/ASSET_SOURCES.md).

Language: port menu (L3 + R3) > LANGUAGE, or `language = uk` (or `en`) in `sotn.ini`. A change needs a restart.
Ten Ukrainian labels (file select, name plate, HUD) need the file `ps1vram_uk.bin` of the Ukrainian Xbox 360 mod in `games/`
at build time. Without it they stay English.

## 7. `sotn.ini`

- The game makes `sotn.ini` next to the program at the first start, with the default settings. You do not create it.
- The port menu writes it at once when you change a setting. You can edit it in a text editor when the game is closed.
- A missing key keeps its default. A line that starts with `;` is a comment.
- The full list of keys is in [README.md](README.md) (section Settings).

## 8. Controls

| PS button | Keyboard | Xbox pad | Switch pad |
|---|---|---|---|
| Cross / Circle | `X` / `D` | A / B | B / A |
| Square / Triangle | `Z` / `S` | X / Y | Y / X |
| L1 / R1 | `Q` / `R` | LB / RB | L / R |
| L2 / R2 | `W` / `E` | LT / RT | ZL / ZR |
| Start / Select | `Return` / `Backspace` | Menu / View | + / - |
| L3 / R3 | `1` / `2` | stick clicks | stick clicks |
| D-pad | Arrow keys | D-pad | D-pad |

- **L3 + R3 (keyboard `1` + `2`) opens the port menu**: language, display, key bindings, quality of life, cheats, teleport map.
- L2 (ZL, `W`) uses the Pocket item. The left stick works as the D-pad. The right stick moves the view.
- `F11` or `F4`: window or fullscreen. `Esc` leaves fullscreen. In a window `Esc` quits the game.
- Keys and pad buttons can be changed in the port menu: CONTROLS > KEY BINDINGS.

## 9. Troubleshooting

First open `sotn.log` next to `sotn.exe` (on the Switch: `/switch/sotn-native/sotn.log`). `sotn.prev.log` is the run before.
The first line shows the version. Send both files with a bug report. Do not send game data.

| Problem | What to do |
|---|---|
| Message "Game data not found: data/BIN/F_TITLE0.BIN" | The `data` folder is missing or in the wrong place. It must be next to the program, with `BIN\F_TITLE0.BIN` inside. Repeat step 2 and 3b. |
| `build.py` says "no PlayStation disc" | Put the `.bin` and `.cue` of SLUS-00067 into `games/`. Read the lines the script prints for each file. |
| `build.py` says "Missing tools" | Run `python3 build.py --install-deps`, or the commands it prints. |
| `build.py` says "Windows line ends" | Clone again with `-c core.autocrlf=false`. |
| Build is very slow | The clone is on a Windows drive. Clone inside WSL (`~/`). |
| No music | `sotn-xa.cue` is missing, or `data\SD\XA_STR1` is missing. Copy `out/pc/sotn-xa.cue` next to the program. |
| The window does not open | Update the video driver (OpenGL 3.3). Read `sotn.log`. |
| No HD pictures | `hd_textures = 1`? Is there `data\textures\<language>\`? Look for `[hd]` lines in `sotn.log`. |
| Game closes at `Esc` | `Esc` quits the game in a window. Use `F11` for fullscreen. |
| Language did not change | It needs a restart. |
| Switch: back to hbmenu at once | Data is missing in `/switch/sotn-native/data/`. See `sotn.log`, line `[boot] data dir`. |

Known state: Saturn stages and Maria as a player are experimental.
`[unverified]` A clean install from a ready package. The Linux clean-install test of the data check passed (only `data/BIN/F_TITLE0.BIN` is tested at start).
