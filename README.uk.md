# sotn-native

Нативний порт Castlevania: Symphony of the Night для ПК (Linux, Windows) і Nintendo Switch.
Його зібрано з декомпіляції [sotn-decomp](https://github.com/Xeeynamo/sotn-decomp).
Він працює на [psyz](https://github.com/Xeeynamo/psyz) — PlayStation SDK для сучасних систем.
Порт додає вміст із версій для Sega Saturn і PSP та український переклад.

English: [README.md](README.md). README оригінального проєкту: [README.upstream.md](README.upstream.md).

**Посібник зі встановлення:** [README_INSTALL.uk.md](README_INSTALL.uk.md) (крок за кроком, Windows і Switch,
[English](README_INSTALL.md)). HD-пакети картинок для uk, en і для обох мов лежать у
`tools/lang/textures/`. Збірка копіює їх у `data/textures/`.

**У цьому репозиторії немає даних гри.** Потрібні ваші власні, легально придбані диски.
Збирання читає їх із вашого диска і створює файли гри на вашому комп'ютері.

## Швидкий старт

1. Отримайте код. Клонуйте його в Linux або у WSL 2 (див. «Windows» нижче):

   ```sh
   git clone --recurse-submodules -b switch https://github.com/rashevskyv/sotn-native.git
   ```

2. Покладіть образи своїх дисків у папку `games/` цього клону. Назви файлів не важливі.
   Збирання впізнає кожен диск за його вмістом.
3. Запустіть збирання:
   - Windows: двічі клацніть `build.bat`. Він запускає збирання у WSL і наприкінці відкриває `out\`.
   - Linux або WSL: `python3 build.py`.

| Образ у `games/` | Файли | Що додає |
|---|---|---|
| PlayStation, США (SLUS-00067) | `.bin` (і `.cue`) | Гру. Обов'язковий. |
| PSP, Castlevania: The Dracula X Chronicles, Європа (ULES-00841) | `.iso` або `.cso` | Марію як гравця, її голос, імена фамільярів із PSP. |
| Sega Saturn, Японія (T-9527G) | `.bin` (і `.cue`) | Етапи Saturn, графіку екрана вибору персонажа. |
| `ps1vram_uk.bin` з українського мода для Xbox 360 | Сам файл | Десять українських написів: вибір файлу, табличка імені, HUD. Необов'язковий. |

Збирання створює дві папки:

| Папка | Що з нею робити |
|---|---|
| `out/sd/` | Скопіюйте її вміст у корінь SD-картки. У ній `switch/sotn-native/` із `sotn.nro` і даними гри. |
| `out/pc/` | Збірка для ПК. Запуск: `out/pc/run.sh` (Linux) або `out\pc\run.bat` (Windows). |

Більше про збирання:

- Якщо інструмента немає, збирання зупиняється. Воно виводить команду, яка його встановлює.
  `python3 build.py --install-deps` виконує ці команди через `sudo` (apt і pacman від devkitPro).
- Параметри: `--pc-only`, `--switch-only`, `--clean` (почати все з нуля).
- Повторний запуск пропускає готові кроки.
- Журнал — `out/build.log`. Після помилки збирання називає крок, на якому вона сталася.
- Перше збирання завантажує модулі Go, пакети Rust і форк SDL від devkitPro. Потрібен інтернет.
- Збирання створює український пакет із цього репозиторію і диска US.
  Десяти написам ще потрібен `ps1vram_uk.bin` (див. «Українська мова»). Без нього вони лишаються англійською.
- В `out/sd/` немає `sotn.ini`. Гра записує його під час першого запуску. Скопійований файл замінив би ваші налаштування.
- Файли гри в `out/` — жорсткі посилання на файли в `build/auto/`. Вони займають місце на диску лише один раз.

### Windows

`build.py` і `build.bat` створюють збірку для Linux. Вона працює у WSL 2, а WSLg показує її вікно.
Є й нативна збірка для Windows (`sotn.exe`). Це ручна збірка MinGW, див. «Збирання для Windows» нижче.
`build.bat` потребує WSL 2 з Ubuntu. Якщо його немає, `build.bat` виводить команду встановлення (`wsl --install -d Ubuntu-24.04`).

Клонуйте репозиторій усередині WSL, наприклад у `\\wsl.localhost\Ubuntu-24.04\home\<користувач>\`.
Клон на диску Windows (`C:\`, `D:\`) теж працює, але збирання там значно повільніше.
Якщо клонуєте через Git for Windows, вимкніть заміну кінців рядків: `git clone -c core.autocrlf=false ...`.

## Що працює

| Частина | Стан |
|---|---|
| Збірка для ПК (Linux, x86-64) з PS1 US | Працює. Основна платформа для перевірки коду. |
| Збірка для ПК (Windows, x86-64), MinGW, один `sotn.exe` | Працює. Ручна збірка (див. «Збирання для Windows»). |
| NRO для Nintendo Switch (homebrew) | Працює. Перевірено на одній консолі. |
| Вигляд 16:9 з цілим масштабом, меню порту, значки кнопок, вібрація при ударі (Switch) | Працює. |
| Етапи Saturn: Підземний сад, Проклята в'язниця та їхні обернені версії; зшиті кімнати | Експериментально. Ворогів і босів перенесено вручну, вони можуть відрізнятися від гри на Saturn. |
| Екран вибору персонажа з Saturn | Працює. Для графіки потрібен диск Saturn. |
| Марія як гравець (з версії PSP) | Експериментально. |
| Імена фамільярів із PSP, дві японські картки фамільярів | Працює (картки — опція). |
| Набори голосів (японський і англійський PSP) | Працює, але див. «Набори голосів»: лише з цього репозиторію їх поки не зробити. |
| Українська мова | Працює. Без `ps1vram_uk.bin` десять написів лишаються англійською (див. «Українська мова»). |
| Чити, список і мапа телепорту, список босів (бій із переможеним босом ще раз), сторінка редактора тайлів (порт 8080, без пароля) | Інструменти для тестів. |

Офлайн-перевірки: `python3 tools/test/run.py offline` (дані гри не потрібні).
Перевірки з грою і перевірки релізу: див. [docs/TESTING.md](docs/TESTING.md). Більшість перевірок гри поки ручні. Можливі помилки.

Інші документи (англійською):

- [docs/ASSET_SOURCES.md](docs/ASSET_SOURCES.md): звідки береться кожна частина даних гри. У git немає нічого пропрієтарного.
- [docs/FONTS.md](docs/FONTS.md): кожен шрифт, де він використаний, його ліцензія.
- [docs/TESTING.md](docs/TESTING.md): перевірки і список перевірок релізу.

## Що потрібно

### Диски

Покладіть образи своїх дисків у `disks/` (див. нижче). sotn-decomp дає кожній версії назву.

| Диск | Назва в sotn-decomp | Обов'язковий | Що додає |
|---|---|---|---|
| PlayStation, США (SLUS-00067) | `us` | Так | Усю гру. |
| PSP, Castlevania: The Dracula X Chronicles, Європа (ULES-00841) | `pspeu` | Ні | Марію як гравця, її голос, імена фамільярів. |
| Sega Saturn, Японія (T-9527G) | `saturn` | Ні | Етапи Saturn, графіку екрана вибору персонажа. |

Збирання перевіряє лише те, що потрібні файли є. Інші видання дисків не перевірено.

### Інструменти (Debian або Ubuntu, також у WSL 2)

- Пакети оригінального проєкту: `sudo apt install $(cat tools/requirements-debian.txt)`.
- CMake 3.28 або новіший, Ninja, компілятор C і C++ (GCC або Clang).
- Rust (`rustup`, стабільний `cargo`). Потрібен для будь-якого збирання.
- Go 1.24 або новіший. Потрібен для будь-якого збирання (інструменти для дисків і ресурсів написано на Go).
- `xxd` (голос Марії), `7z` (PSP), `bchunk` (Saturn), `ffmpeg` з декодером ATRAC3plus (набори голосів).
- Python 3 з Pillow (з FreeType і libraqm), PyYAML і numpy:
  `sudo apt install python3-pil python3-yaml python3-numpy libraqm0`.
  Перевірка: `python3 -c "from PIL import features; print(features.check('freetype2'), features.check('raqm'))"` виводить `True True`.
- Для збирання під Linux: пакети розробки X11 або Wayland, потрібні SDL3
  (див. документацію SDL3; SDL збирається з `tools/psyz/external/SDL`).

Для збирання під Switch також потрібно:

- [devkitPro](https://devkitpro.org/wiki/Getting_Started) з `switch-dev`, `switch-mesa` і `switch-libdrm_nouveau`
  (`sudo dkp-pacman -S switch-dev switch-mesa switch-libdrm_nouveau`).
- Форк SDL3 від devkitPro, гілка `switch-sdl-3.4`, з латкою з цього репозиторію:

  ```sh
  git clone -b switch-sdl-3.4 https://github.com/devkitPro/SDL.git ~/SDL-switch
  git -C ~/SDL-switch am "$PWD/tools/sdl-switch/0001-switch-SetWindowSize-uses-the-pending-size.patch"
  ```

  Другу команду запускайте з кореня цього репозиторію.

## Як усе поєднано

```mermaid
flowchart LR
    subgraph discs[Ваші диски]
        PS1[PS1 US<br>sotn.us.cue + .bin]
        PSP[PSP EU<br>sotn.pspeu.iso]
        SAT[Saturn JP<br>sotn.saturn.cue + .bin]
    end
    PS1 -->|make extract_disk| US[disks/us]
    PSP -->|make extract_disk VERSION=pspeu| PSPD[disks/pspeu]
    SAT -->|make extract-disk VERSION=saturn| SATD[disks/saturn]
    US -->|make extract| ASSETS[assets/]
    US & ASSETS & PSPD & SATD --> CMAKE[Збирання CMake<br>pcdata, Марія, етапи Saturn, пакет uk]
    CMAKE --> PC[build/pc/sotn<br>+ build/pc/data]
    CMAKE --> NRO[build/switch/sotn.nro<br>+ build/switch/data]
    NRO -->|копіювати на SD-картку| SD[/switch/sotn-native/]
```

## Збирання вручну

Це ті кроки, які `build.py` робить за вас.

### 1. Отримайте код

```sh
git clone --recurse-submodules -b switch https://github.com/rashevskyv/sotn-native.git
cd sotn-native
```

### 2. Покладіть диски в `disks/`

- PS1: файл `.cue` і його доріжки `.bin`. Перейменуйте cue-файл на `disks/sotn.us.cue`.
  Рядки `FILE` у cue мають вказувати на файли `.bin` (відносно `disks/`).
- PSP: `disks/sotn.pspeu.iso`.
- Saturn: `disks/sotn.saturn.bin` і `disks/sotn.saturn.cue`.

### 3. Розпакуйте диски

```sh
make extract_disk                       # PS1 -> disks/us
make extract_disk VERSION=pspeu         # PSP -> disks/pspeu (необов'язково)
make extract-disk VERSION=saturn        # Saturn -> disks/saturn (необов'язково)
make python-dependencies                # Python-інструменти sotn-decomp, у .venv
make extract                            # ресурси PS1 -> assets/
make build_assets                       # assets -> src/**/gen/ (файли C, не в git)
```

З образу PSP потрібні лише ці файли:
`PSP_GAME/USRDIR/res/ps/PSPBIN/dra.bin`, `maria.bin` і `res/ps/snd_e/SE/sd_mar2.spk`.

### 4. Збирання для ПК (Linux)

```sh
cmake -S . -B build/pc -G Ninja -DENABLE_ASAN=OFF -DPSYZ_RENDERER=sdl3_gl \
      -DENABLE_SATURN_STAGES=ON
cmake --build build/pc
```

- Приберіть `-DENABLE_SATURN_STAGES=ON`, якщо диска Saturn немає.
- Марія вмикається сама, коли є `disks/pspeu/.../maria.bin` (`-DUSE_PLAYER_MARIA=OFF` вимикає її).
- Необов'язково: `-DSOTN_CHARSEL_ART=<тека>` з вашими портретами для екрана вибору персонажа
  (`alucard.webp`, `richter.webp`, `maria_psp.png`, `maria_psx.png`). Замість відсутнього файлу буде портрет із Saturn.
- `make pc` — короткий спосіб зібрати з типовими опціями.

Запуск:

```sh
build/pc/sotn
```

Гра читає файли з `build/pc/data` (копія `disks/us` плюс етапи Saturn).
Вікно 1280x720, вигляд 16:9 з масштабом 3. `--wide N` задає інший масштаб.
Налаштування: `build/pc/sotn --ini sotn.ini` читає і записує файл налаштувань (див. «Налаштування»).

### 5. Збирання для Nintendo Switch

```sh
export DEVKITPRO=/opt/devkitpro
cmake -S . -B build/switch -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake -DCMAKE_BUILD_TYPE=Release \
      -DENABLE_ASAN=OFF -DPSYZ_RENDERER=sdl3_gl -DPSYZ_SDL_DIR=$HOME/SDL-switch \
      -DENABLE_SATURN_STAGES=ON
cmake --build build/switch
```

Результат — `build/switch/sotn.nro`. Ті самі необов'язкові опції, що й для ПК.

### 5a. Збирання для Windows

Збірка для Windows — це крос-збирання з MinGW-w64 (`sudo apt install mingw-w64`) у Linux або WSL:

```sh
cmake -S . -B build/win -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=tools/psyz/psyz/cmake/windows-x86_64.cmake -DCMAKE_BUILD_TYPE=Release \
      -DENABLE_ASAN=OFF -DPSYZ_RENDERER=sdl3_gl -DPSYZ_STATIC_MODULES=ON -DSOTN_PC_DATA_DIR=data \
      -DENABLE_SATURN_STAGES=ON
cmake --build build/win
```

Результат — `build/win/sotn.exe`: один файл, без DLL, без вікна консолі.
Покладіть його в теку з файлами гри в `data/` (як `out/pc/`). Журнал — `sotn.log` поруч із ним.

### 6. Встановлення на Switch

Порт має власну теку на SD-картці: `/switch/sotn-native/`.

| Шлях на SD-картці | Що це | Звідки |
|---|---|---|
| `/switch/sotn-native/sotn.nro` | Програма | `build/switch/sotn.nro` |
| `/switch/sotn-native/data/` | Файли гри | `build/switch/data/` (усе) |
| `/switch/sotn-native/data/ST/<STAGE>/F_<STAGE>.BIN` | Зшиті кімнати Saturn (лише з етапами Saturn) | `build/switch/saturn_stitches/disks/us/ST/` (скопіюйте поверх `data/ST/`) |
| `/switch/sotn-native/voice/jp/`, `voice/psp/` | Набори голосів (необов'язково) | див. «Набори голосів» |
| `/switch/sotn-native/sotn.ini` | Налаштування | гра пише його під час першого запуску |

Гра також пише туди: `sotn-xa.cue` (музика; вказує на `data/SD/XA_STR1`),
`sotn.log`, `sotn.prev.log`, `sotn.out.log` і картки пам'яті `bu00/` та `bu10/`.
Інших файлів не потрібно. Запускайте порт із меню homebrew.

## Налаштування (`sotn.ini`)

Якщо файлу немає, гра записує його з типовими значеннями.
Більшість значень змінює меню порту в грі. Відсутній ключ має типове значення.

| Ключ | Значення | Типово | Дія |
|---|---|---|---|
| `scale` | від 1 до 8, `max` | 3 | Цілий масштаб пікселя для вигляду 16:9. `max` завжди бере найбільший масштаб, що вміщається на екрані. |
| `fullscreen` | 0, 1 | 0 | ПК: запуск у безрамковому повноекранному режимі. F11 (або F4) перемикає його в грі; Esc виходить із нього, другий Esc закриває гру. |
| `voice` | `ps1`, `jp`, `psp` | `ps1` | Набір голосів. `jp` і `psp` потребують кліпів у `voice/`; замість відсутнього кліпу звучить голос PS1. |
| `language` | `en`, `uk` | `en` | Мова. Зміна потребує перезапуску. `uk` без пакета показує англійську. |
| `buttons` | `switch`, `xbox`, `psx` | `switch` на Switch, `xbox` на ПК | Значки кнопок у меню. |
| `jp_cards` | `extra`, `jp`, `off` | `extra` | Дві японські картки фамільярів: поруч із предметами США, на місцях JP/PSP або без них. Читається під час запуску. |
| `relic_toggle` | `modern`, `stock` | `modern` | Вигляд перемикача реліквій. |
| `skip_cd_rooms` | 0, 1 | 0 | 1: двері в CD-кімнату завантаження (коридор між двома зонами) ведуть одразу в наступну зону. 0: гравець проходить кімнату, як на диску. |
| `show_loading_text` | 0, 1 | 1 | 1: під час завантаження етапу видно напис NOW LOADING («Загрузка») і логотип. 0: екран лишається чорним. |
| `room_edge_fade` | 0, 1 | 1 | 1: у кімнаті, вужчій за широкий кадр, за її лівим і правим краєм нічого не видно (чорне), а крайній тайл темніє до чорного на зовнішніх 8 пікселях. 0: стара заливка (сусідня кімната, дзеркальне відображення) під широким темним градієнтом. |
| `room_stitch` | 0, 1 | 0 | Тест: деякі сусідні кімнати грають як одна широка кімната. |
| `spawn` | назва або порожньо | порожньо | Тестова точка старту: `np3-stage15-door`, `marble-soul`, `caverns-soul`, `reverse-trapdoor-underground`, `reverse-marble-soul`, `reverse-caverns-soul`. |
| `cheat_refill` | 0, 1 | 0 | HP, MP і серця завжди повні. |
| `cheat_invincible` | 0, 1 | 0 | Удари не шкодять. |
| `cheat_walls` | 0, 1 | 0 | Туман проходить крізь будь-яку стіну і не закінчується. |
| `cheat_items` | 0, 1 | 0 | Уся зброя, предмети і реліквії. |
| `cheat_map` | 0, 1 | 0 | Уся мапа обох замків. |
| `cheat_skip_prologue` | 0, 1 | 0 | Нова гра починається за Алукарда біля брами замку. |
| `cheat_character` | 0, 1, 2, 4 | 0 | Хто грає в завантаженому збереженні: 0 — правило гри, 1 — Алукард, 2 — Ріхтер, 4 — Марія. |
| `editor` | 0, 1 | 0 | Сторінка редактора тайлів на порту 8080. Без пароля: вмикайте лише в довіреній мережі. |
| `overlay` | 0, 1 | 0 | У куті видно етап, кімнату і чити. |
| `fps` | 0, 1 | 0 | У куті видно кадри за секунду. |

Ключ `aspect` зі старих файлів ігнорується: вигляд завжди 16:9.
Етапи Saturn і Марія — опції збирання, а не налаштування. Вібрація завжди ввімкнена (Switch).

## Набори голосів

`tools/voicepack/extract_voice.py` створює `voice/jp/` і `voice/psp/` з образу PSP.
Йому потрібні два файли-маніфести, що зіставляють кожен звук із кліпом в образі.
Маніфестів поки немає в цьому репозиторії, тож лише з нього набори голосів не зробити.
Без них гра використовує голоси PS1.
План маніфестів у git (без аудіо, без тексту): [docs/ASSET_SOURCES.md](docs/ASSET_SOURCES.md).

## Українська мова

Текст, шрифти і намальовані написи лежать у `tools/lang/uk/` (див. `tools/lang/FORMAT.md`).
Збирання створює пакет із цих файлів і диска US.
Десять написів містять пікселі оригінальних зображень: п'ять заголовків вибору файлу, табличка імені і таблички HUD.
Цих десяти написів немає в git. Вони беруться з `ps1vram_uk.bin` українського мода для Xbox 360.
Покладіть цей файл у `games/`, і збирання їх додасть. Без нього ці написи лишаються англійською.
Файли мода поки не публічні.

## Подяки

- [sotn-decomp](https://github.com/Xeeynamo/sotn-decomp): Luciano Ciccariello (Xeeynamo) і всі учасники. Інструменти, яким вони дякують, — у [README.upstream.md](README.upstream.md).
- [psyz](https://github.com/Xeeynamo/psyz) від Xeeynamo (MIT), з SDL (libsdl.org) і cimgui.
- sotn-vita від artesanalintelligence: шаблони даних для ПК (`tools/pcdata`), підключення босів і схема статичних модулів psyz.
- sotn-ultimate: етапи Saturn і екран вибору персонажа перенесено з його перекладу коду Saturn.
- devkitPro і його форк SDL для Switch.
- [Alegreya Sans](https://github.com/huertatipografica/Alegreya-Sans) від The Alegreya Sans Project Authors (SIL Open Font License 1.1, `tools/lang/fonts/OFL-AlegreyaSans.txt`).
- Picoripi: інструмент українського перекладу.

Castlevania і Symphony of the Night — торгові марки Konami. Проєкт не пов'язаний із Konami.

## Ліцензія

GNU Affero General Public License v3 (див. [LICENSE](LICENSE)), як і sotn-decomp.
Підмодулі та шрифти мають власні ліцензії.
