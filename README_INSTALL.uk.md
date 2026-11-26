# Посібник зі встановлення

English: [README_INSTALL.md](README_INSTALL.md). Огляд проєкту: [README.uk.md](README.uk.md).

**У цьому репозиторії немає ігрових даних.** Потрібен ваш власний диск Castlevania: Symphony of the Night
(PlayStation, США, SLUS-00067). Збірка читає образ вашого диска і створює ігрові файли на вашому комп'ютері.
Не просіть образи дисків чи ігрові дані. Не поширюйте їх.

Рядок із позначкою `[unverified]` не пройшов повну перевірку. Інші кроки використовують ті самі скрипти й команди,
якими створено папку гри власника (збірка 0.8.113).

## 1. Що потрібно

| Що | Деталі |
|---|---|
| Образ вашого диска | PS1 США, SLUS-00067: файл `.cue` і його `.bin` (дамп вашого власного диска; треки 1 і 2 — це нормально). Обов'язково. |
| Додаткові диски | PSP «The Dracula X Chronicles», Європа, ULES-00841 (`.iso` або `.cso`): Марія. Sega Saturn, Японія, T-9527G (`.bin` і `.cue`): етапи Saturn. |
| Хост для збірки | Ubuntu 24.04 або Debian. У Windows це WSL 2 (крок 2). `[unverified]` Вільне місце на диску для збірки не вимірювали. |
| Windows | 64-бітна Windows 10 або 11, драйвер відео з OpenGL 3.3. `[unverified]` Найнижча версія Windows. |
| Switch | Консоль з Atmosphère і hbmenu, SD-карта. `[unverified]` Найнижчі версії прошивки й Atmosphère. |

Образи CHD, PBP, 7z, ZIP, RAR і ECM не читаються. Спершу перетворіть їх на `.bin` і `.cue`
(для CHD: `sudo apt install mame-tools; chdman extractcd -i game.chd -o game.cue`).

## 2. Створення ігрових файлів (усі платформи)

Ігрові файли (`data/`) беруться з вашого диска. Усе робить скрипт збірки.

Лише для Windows, один раз, в адміністраторському PowerShell; потім перезавантажте Windows і один раз відкрийте «Ubuntu», щоб створити користувача:

```powershell
wsl --install -d Ubuntu-24.04
```

Далі в Ubuntu (WSL) або в Linux:

```sh
git clone -c core.autocrlf=false --recurse-submodules -b switch https://github.com/rashevskyv/sotn-native.git ~/sotn-native
cd ~/sotn-native
# покладіть образи дисків у games/ (будь-які імена файлів). Диск C: у WSL — це /mnt/c/
cp "/mnt/c/Users/<ви>/Downloads/Castlevania SOTN (USA) (Track 1).bin" games/
cp "/mnt/c/Users/<ви>/Downloads/Castlevania SOTN (USA).cue" games/
# необов'язково: інші треки, образ PSP (.iso), Saturn (.bin і .cue) також у games/
python3 build.py --install-deps
```

- Клон має лежати у файловій системі WSL (`~/`), не на `C:` чи `D:` (там збірка набагато повільніша).
- `--install-deps` ставить відсутні інструменти через `sudo` і потребує мережі (apt, модулі Go, крейти Rust).
  Без нього скрипт друкує команди встановлення і зупиняється.
- Скрипт визначає кожен диск за вмістом, а не за іменем файлу, і друкує, що знайшов.
- Опції: `--pc-only` (без збірки Switch, devkitPro не потрібен), `--switch-only`, `--clean`.
- Журнал: `out/build.log`. Повторний запуск пропускає виконані кроки.

Що створює збірка:

| Папка | Що |
|---|---|
| `out/pc/` | Лінуксова збірка для ПК із папкою `data/`, `sotn-xa.cue` і `run.sh`. Запуск: `out/pc/run.sh`. У Windows `out\pc\run.bat` запускає її в WSL. |
| `out/sd/` | Збірка для Switch: `switch/sotn-native/sotn.nro` з її `data/`. |

Папка `data/` в обох однакова: файли вашого диска, HD-картинки з цього репозиторію (крок 6)
і, якщо є диск Saturn, зшиті кімнати Saturn.
Усередині збірки файли диска створює `bin/sotn-disk extract <sotn.cue> disks/us`
(`make bin/sotn-disk` будує інструмент; потрібен Go 1.24). `[unverified]` Для цього посібника інструмент повторно не запускали.

## 3. Windows

### 3a. Збірка `sotn.exe`

`build.py` робить лінуксову збірку. Рідна програма для Windows — це крос-збірка MinGW.
Запустіть її в WSL, у тому самому клоні, після кроку 2 (потрібні `disks/us` і `assets/`, які створив `build.py`):

```sh
sudo apt install mingw-w64
cd ~/sotn-native
cmake -S . -B build/win -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=tools/psyz/psyz/cmake/windows-x86_64.cmake -DCMAKE_BUILD_TYPE=Release \
      -DENABLE_ASAN=OFF -DPSYZ_RENDERER=sdl3_gl -DPSYZ_STATIC_MODULES=ON -DSOTN_PC_DATA_DIR=data \
      -DENABLE_SATURN_STAGES=ON
cmake --build build/win
```

- Приберіть `-DENABLE_SATURN_STAGES=ON`, якщо диска Saturn немає.
- Марія вмикається сама, коли ви дали образ PSP.
- Необов'язково: `-DSOTN_ICON=<файл.ico>` для значка програми.
- Результат: `build/win/sotn.exe` (один файл, без DLL, без вікна консолі) і `build/win/data/` (близько 670 МБ разом з HD-картинками).

### 3b. Папка для Windows

```sh
mkdir -p /mnt/c/Games/sotn-native
cp build/win/sotn.exe /mnt/c/Games/sotn-native/
cp -r build/win/data /mnt/c/Games/sotn-native/
cp out/pc/sotn-xa.cue /mnt/c/Games/sotn-native/
# лише з диском Saturn: зшиті кімнати замінюють файли в data/ST
cp -r build/win/saturn_stitches/disks/us/ST/. /mnt/c/Games/sotn-native/data/ST/
```

Запустіть `C:\Games\sotn-native\sotn.exe`. Файл `sotn.ini` створюється при першому запуску зі стандартними налаштуваннями.

```text
C:\Games\sotn-native\
|-- sotn.exe          програма
|-- sotn.ini          налаштування; створює гра, пише меню порту
|-- sotn-xa.cue       3 рядки, що вказують на data\SD\XA_STR1 (музика диска)
|-- data\             ігрові дані з вашого диска
|   `-- BIN\ BOSS\ SD\ SERVANT\ ST\ VAB\ XA\ і файли диска (DRA.BIN, ...)
|-- music\            необов'язково, покращена музика (крок 5)
|-- voice\            необов'язково, набори голосів jp\ psp\ (крок 5)
|-- bu00\ bu10\       збереження карти пам'яті (створює гра)
`-- sotn.log, sotn.prev.log, sotn.out.log
```

Якщо гра не знаходить `data\BIN\F_TITLE0.BIN`, вона показує повідомлення «Game data not found» і зупиняється.
Лише цей файл перевіряється при запуску. `sotn-xa.cue`, `music\` і `voice\` необов'язкові.
Збірка вбудовує HD-пакети в програму. Вона не створює теку `data\textures\`.
Без `sotn-xa.cue` гра не відтворює музику диска. Вміст файла cue:

```text
FILE "data/SD/XA_STR1" BINARY
  TRACK 01 MODE2/2352
    INDEX 01 00:00:00
```

У cue можна вказати `data/SD/XA_STR1` або `data/SD/XA_STR1.`.
Гра відкриває ту назву, яка існує.

## 4. Nintendo Switch

Скопіюйте вміст `out/sd/` у корінь SD-карти (з WSL: `cp -r out/sd/. /mnt/<диск>/`, або через Провідник: `\\wsl.localhost\Ubuntu-24.04\home\<ви>\sotn-native\out\sd\`).
Вручну результат має бути таким:

```text
/switch/sotn-native/
|-- sotn.nro          програма. Ім'я файла має бути саме sotn.nro
|-- data/             ігрові дані з вашого диска, з data/textures/{en,uk,common}/
|-- music/            необов'язково (крок 5)
|-- voice/            необов'язково (крок 5)
|-- sotn.ini          гра створює його при першому запуску
|-- sotn-xa.cue       гра пише його при кожному запуску
|-- bu00/ bu10/       збереження карти пам'яті
`-- sotn.log, sotn.prev.log, sotn.out.log
```

1. Вставте SD-карту в консоль. Запустіть Atmosphère.
2. Відкрийте hbmenu і запустіть «Castlevania: SOTN». `[unverified]` Запускайте hbmenu через ігровий тайтл (повна пам'ять). Режим аплету альбому не тестували.
3. EXIT GAME у меню порту повертає в hbmenu.

Порт використовує власну папку і не чіпає `/switch/sotn/`. Не копіюйте `sotn.ini` з іншої інсталяції.
Якщо гра одразу повертається в hbmenu, прочитайте `/switch/sotn-native/sotn.log` і знайдіть рядок `[boot] data dir`.
`[unverified]` Скільки вільного місця потрібно на SD. `data/` займає близько 670 МБ (збірка для Windows з етапами Saturn).

## 5. Необов'язково: музика і голос

**Музика (`music/`).** Музика диска працює завжди. «Покращена» музика замінює її вашими файлами.
У папці лежать `<id треку>.mp3` або `.flac` (44100 Гц) і необов'язковий `loops.txt`.
Інструмент `tools/music/ost_map.py` створює папку з офіційного альбому саундтреку (KICA-7760), який у вас є:

```sh
python3 -I tools/music/ost_map.py copy <папка з 34 файлами "NN Title.mp3"> <вихід>/music
```

Скопіюйте цю папку `music` поруч із `sotn.exe` (або в `/switch/sotn-native/`), потім у меню порту оберіть MUSIC = ENHANCED.
Аудіо в репозиторії немає.

**Голос (`voice/jp/`, `voice/psp/`).** Це кліпи `<ID>.pcm` (сирі знакові 16 біт, моно, 37800 Гц) для наборів голосів
японського і PSP англійського. `build.py` робить їх з вашого образу PSP через `tools/voicepack/extract_voice.py`, але потрібні
файли `manifest_voice_jp.json` і `manifest_voice_pspen.json` у `games/`. У репозиторії їх немає.
`[unverified]` Без них набори jp і psp відтворюють англійський голос PS1. Гра працює.
Коли папка `out/pc/voice/` існує, скопіюйте її поруч із програмою.

## 6. HD-пакети текстур і мова

HD-картинки є частиною цього репозиторію: `tools/lang/textures/{uk,en,common}` (PNG і пакований `.hdt`).
Збірка сама копіює їх у `data/textures/<мова>/`. Нічого завантажувати не треба.

- Картинки з `uk/` показуються з українською мовою, з `en/` — з англійською. Картинки з `common/` — з обома.
- Вимкнути: `hd_textures = 0` у `sotn.ini` (або меню порту, DISPLAY, HD TEXTURES).
- Без папки гра працює з вбудованими картинками. Подробиці: [docs/ASSET_SOURCES.md](docs/ASSET_SOURCES.md).

Мова: меню порту (L3 + R3) > LANGUAGE, або `language = uk` (чи `en`) у `sotn.ini`. Зміна потребує перезапуску.
Десять українських написів (вибір файла, табличка імені, HUD) потребують під час збірки файла `ps1vram_uk.bin`
з українського мода Xbox 360 у `games/`. Без нього вони лишаються англійською.

## 7. `sotn.ini`

- Гра сама створює `sotn.ini` поруч із програмою при першому запуску зі стандартними налаштуваннями. Створювати його не треба.
- Меню порту записує файл одразу, коли ви змінюєте налаштування. Можна редагувати його в текстовому редакторі, поки гра закрита.
- Відсутній ключ лишає стандартне значення. Рядок, що починається з `;`, — коментар.
- Повний список ключів: [README.uk.md](README.uk.md) (розділ про налаштування).

## 8. Керування

| Кнопка PS | Клавіатура | Геймпад Xbox | Геймпад Switch |
|---|---|---|---|
| Cross / Circle | `X` / `D` | A / B | B / A |
| Square / Triangle | `Z` / `S` | X / Y | Y / X |
| L1 / R1 | `Q` / `R` | LB / RB | L / R |
| L2 / R2 | `W` / `E` | LT / RT | ZL / ZR |
| Start / Select | `Return` / `Backspace` | Menu / View | + / - |
| L3 / R3 | `1` / `2` | натиск стиків | натиск стиків |
| Хрестовина | стрілки | хрестовина | хрестовина |

- **L3 + R3 (клавіатура `1` + `2`) відкриває меню порту**: мова, екран, призначення клавіш, зручності, чити, карта телепорту.
- L2 (ZL, `W`) використовує предмет Кишені. Лівий стик працює як хрестовина. Правий стик рухає огляд.
- `F11` або `F4`: вікно чи повний екран. `Esc` виходить із повного екрана. У вікні `Esc` закриває гру.
- Клавіші й кнопки змінюються в меню порту: CONTROLS > KEY BINDINGS.

## 9. Вирішення проблем

Спершу відкрийте `sotn.log` поруч із `sotn.exe` (на Switch: `/switch/sotn-native/sotn.log`). `sotn.prev.log` — попередній запуск.
Перший рядок показує версію. До звіту про помилку додайте обидва файли. Не надсилайте ігрові дані.

| Проблема | Що робити |
|---|---|
| Повідомлення «Game data not found: data/BIN/F_TITLE0.BIN» | Папки `data` немає або вона не там. Вона має бути поруч із програмою, з `BIN\F_TITLE0.BIN` всередині. Повторіть кроки 2 і 3b. |
| `build.py` пише «no PlayStation disc» | Покладіть `.bin` і `.cue` диска SLUS-00067 у `games/`. Прочитайте рядки, які скрипт друкує про кожен файл. |
| `build.py` пише «Missing tools» | Запустіть `python3 build.py --install-deps` або надруковані команди. |
| `build.py` пише про «Windows line ends» | Склонуйте знову з `-c core.autocrlf=false`. |
| Збірка дуже повільна | Клон лежить на диску Windows. Клонуйте всередині WSL (`~/`). |
| Немає музики | Немає `sotn-xa.cue` або `data\SD\XA_STR1`. Скопіюйте `out/pc/sotn-xa.cue` поруч із програмою. |
| Вікно не відкривається | Оновіть драйвер відео (OpenGL 3.3). Прочитайте `sotn.log`. |
| Немає HD-картинок | Чи `hd_textures = 1`? Чи є `data\textures\<мова>\`? Шукайте рядки `[hd]` у `sotn.log`. |
| Гра закривається на `Esc` | У вікні `Esc` закриває гру. Для повного екрана використовуйте `F11`. |
| Мова не змінилась | Потрібен перезапуск. |
| Switch: одразу назад у hbmenu | Немає даних у `/switch/sotn-native/data/`. Див. `sotn.log`, рядок `[boot] data dir`. |

Стан: етапи Saturn і Марія як гравець — експериментальні.
`[unverified]` Чиста інсталяція з готового пакета. Тест чистої інсталяції на Linux для перевірки даних пройдено (при запуску перевіряється лише `data/BIN/F_TITLE0.BIN`).
