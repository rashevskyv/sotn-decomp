# Testing

Three levels. Level 1 needs no game data. Levels 2 and 3 need your disc images, so they run only on your machine.

| Level | Needs | Run | State |
|---|---|---|---|
| 1 Offline | git only | `python3 tools/test/run.py offline` | Works |
| 2 Game, hidden | a Linux build and its `data/` | `python3 tools/test/run.py game` | PLANNED. The driver works today. |
| 3 Local build | the images in `games/` | `python3 tools/test/run.py build` | PLANNED |

"PLANNED" marks a test or a log line that is designed but not built yet.
Where the data comes from: [ASSET_SOURCES.md](ASSET_SOURCES.md).

Rules:
- The game runs hidden under Xvfb, with `SDL_AUDIODRIVER=dummy`. No window opens on the desktop.
- Tests use a private copy in `~/sotn/runs/test/t`. They never touch the owner's play folder.
- A passing test deletes its screenshots and logs. A failing test keeps `game.log` and at most 3 screenshots.
- Never start a test while the owner's game runs from the same folder.

## Level 1: offline

`python3 tools/test/run.py offline [--textures <dir>] [-k <name>]`.
`--textures` names a folder with `uk/`, `en/` and `common/` (for example `<game>/data/textures`).
Without it the two HD tests skip. `-k` runs only the tests whose name holds the text.
Each test prints `PASS`, `FAIL` or `SKIP` and a reason. The exit code is 1 if a test fails.

| Test | Asserts |
|---|---|
| `lang_selftest` | `tools/lang/sotn_lang.py selftest` prints `selftest ok` |
| `wide_maths` | `src/pc/wide_test.c`, compiled in a temp folder, exits 0 |
| `script_tables` | Each translation file parses. Each box key exists in `places.json`, except the 13 known boxes in `run.py` (`UNPLACED`). Each line fits 24 cells. `ua_jp.json` and `ua_psp.json` hold only boxes that differ from `ua.json`. `en_jp.json` covers every box of `ua.json`. A missing `ua_jp.json` or `en_jp.json` skips its part. |
| `hd_names` | Each PNG name in a pack folder matches the loader pattern. No file is larger than 2048 px. |
| `hd_parity` | Each uk HD file has an en file with the same name and rect, or a line in `tools/test/hd_parity_exceptions.txt` with a reason. The same for en. No key is in both a language folder and `common/`. |
| `no_stock_in_git` | `sotn_lang.py guard` reports 0 problems (needs the owner's source folders in `SOTN_LANG_XBLA`; skipped without them). No `.pcm`, `.mp3`, `.flac`, `.at3`, `art.bin`, `ps1vram_uk.bin` or HD PNG is tracked. |

The 24-cell check splits a box at `\n` and at a run of two or more `{}` ops (a new speaker).
The exact check is `sotn_lang.py width`. It needs the owner's source folders.

## Level 2: the game, hidden

Setup: `tools/test/setup.sh [<build dir>]` makes the private copy from `build/linux-static`.
It links the big data and copies only the program and the memory cards.
Driver: `tools/test/drive.py <W>x<H> <ini> <outdir> step...`
Steps: `k:KEY[,KEY]` tap, `h:KEY:SEC` hold, `w:SEC` wait, `s:NAME` screenshot, `f:` fit the window.
Key names are xdotool names (`Return`, `Right`, `z`).
`SOTN_ARGS` adds arguments to the game, for example `SOTN_ARGS="--stage st0"`.
`SOTN_TEST_DIR` names another private copy. `SOTN_TEST_DISPLAY` names another Xvfb display (default `:100`).
Exit code: 0 when all steps ran, 2 when the game ended before the last step.

The log lines `[lang] script` and `[dlg] box` and the variable `SOTN_TEST_VOICE_AT` are PLANNED (task 4).
All tests in this section are PLANNED.

| Test | Runs | Asserts |
|---|---|---|
| `script_voice_matrix` | 6 runs: language en, uk x voice ps1, jp, psp. Start at `--stage st0`, walk to Dracula, play the dialogue. | The log line `[lang] script` names the file of the table in ASSET_SOURCES.md. Each `[dlg]` line carries the box hash of that file. Each `[voice]` line names the set and says `ok`. |
| `voice_change_mid_dialogue` | 4 runs (uk and en; ps1 -> psp, jp -> ps1). `SOTN_TEST_VOICE_AT=3:psp` changes the voice before box 3. | Boxes 0 to 2 have the hashes of the first table. Boxes 3 and later have the hashes of the second table. The `[voice]` lines change set at the same box. No crash. The dialogue ends. |
| `hd_per_language` | Flow title -> menu -> file select -> name entry -> player select, and each of the 13 banner stages, in uk and in en. | A uk run loads no file from `textures/en`, and the reverse. Each pack file of a visited screen has one `loaded` line and one `on` line. |
| `hd_no_leftover` | Same runs. | No `[hd] ... no file` line, except the lines in `tools/test/hd_known_nofile.txt`. No `too many rects`, no `larger than`, no `hidden by a primitive`, no SDL error. |
| `hd_no_double_load` | Same runs. | For each path: `loaded` lines minus `released` lines is 0 or 1 at every point of the log. |
| `hd_off` | One run with `hd_textures = 0`. | No `loaded` line. The flow ends. |
| `degrade_runtime` | Private copy without `voice/`, `music/`, `data/textures/`, `lang/`. Runs: en+psp, uk+jp. | The game does not exit. `[voice] ... missing` appears and the dialogue ends. `[hd] ...: no folder`. `[music] ... the XA plays`. en shows stock text; uk shows `script.bin`. No `[crash]` line. |
| `boot_version` | One run to the title. | `[boot] sotn-native <v>` equals `SOTN_VERSION` of `CMakeLists.txt`. |

## Level 3: the local build

`build.py --packs-only --games <dir> --out <dir>` runs only the pack steps (PLANNED option).

| Test | Input | Asserts |
|---|---|---|
| `packs_full` | all images, soundtrack, `local/` art | Exit 0. `voice/jp` 183 files, `voice/psp` 179 files. `music/` 34 files and `loops.txt`. `lang/en/script_psp.bin` exists. `data/textures/uk`, `en`, `common` are filled. |
| `packs_ps1_only` | only the PS1 image | Exit 0. One line per missing input, each names what is left out. No `voice/`, no `lang/en/`. |
| `packs_wrong_psp` | a PSP image with another id | Exit 0. The image is named and skipped, or its clips fail SHA-256 one by one. No partial `.pcm` stays. |
| `packs_no_ffmpeg` | `PATH` without ffmpeg | Exit 0 for the build. Voices are left out with one line. |
| `packs_no_images` | empty `games/` | Exit 1 with "no PlayStation disc". No traceback. |
| `packs_twice` | second run | All pack steps are skipped by their stamps. |

## Release checklist

`python3 tools/test/release_check.py [--version V] [--release <dir>] sotn.exe sotn.nro` does checks 2, 3, 4, 5, 7 (the folder part) and 8.
It prints `PASS`, `FAIL` or `SKIP` per check and the SHA-1 of each file. The exit code is 1 if a check fails.
Without `--version` it takes `SOTN_VERSION` from `CMakeLists.txt`.

| # | Check | How |
|---|---|---|
| 1 | Level 1 passes (level 2: PLANNED) | `python3 tools/test/run.py offline` |
| 2 | Version raised | `SOTN_VERSION` in `CMakeLists.txt`; the same in `[boot]` of the exe log and in the NRO NACP |
| 3 | No own build path in the NRO | `strings -a sotn.nro | grep -c /home/xhr` is 0. devkitPro newlib paths (`/home/davem/...`) are fine. For `sotn.exe` the count is info only (known, fine). |
| 4 | Rumble is in the NRO | `strings -a sotn.nro | grep -ci rumble` is not 0 (4 in 0.8.55 and 0.8.57) |
| 5 | Windows exe is one file | `objdump -p sotn.exe | grep "DLL Name"` lists no libgcc and no libwinpthread; subsystem is Windows GUI |
| 6 | Windows exe starts | Start it hidden in a private copy; `[boot]` appears; no `[crash]` line after 30 s |
| 7 | No stock data in the release | The release folder holds no `data/`, `voice/`, `music/`, `data/textures/`, `lang/en/`; `sotn_lang.py guard` is clean |
| 8 | SHA-1 of both files is in the build report | `sha1sum sotn.exe sotn.nro` |
| 9 | Windows install | Back up the `sotn.exe` of the owner's Windows play folder, then copy. Skip if the game runs. |
| 10 | Switch deploy | Upload, read back, compare SHA-1. The owner tests on the console. |
| 11 | Owner check list | Short list of what changed and what to look at |
| 12 | Docs | `ASSET_SOURCES.md` and `FONTS.md` have a row for each new asset or font |

## Not automated

- Sound: tests run with a dummy audio device. A person checks that a voice clip matches its text.
- The Switch: no test runs on the console or in an emulator.
- The look of HD art: a person compares screenshots.
