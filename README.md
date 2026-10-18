# Azimuth Keyboard Mod

[English](#english) | [Русский](#russian)

<a id="english"></a>

## English

Keyboard controls for **Azimuth 0.9.10, Windows x64**, with three configurable virtual gamepad presets.

### Installation

1. Download the ready-to-use mod archive from [Releases](https://github.com/Aleksey-Kon/Azimuth-Keyboard-Mod/releases).
2. Extract the contents of the mod folder **into the emulator root**, where `azimuth.exe` is located. Place `Start-Azimuth-Gamepad.exe` next to `azimuth.exe` and `data/gamepad/` inside the emulator's `data` folder. Merge the `data` folders when copying.
3. Run `Start-Azimuth-Gamepad.exe` and select a preset under **Gamepad Controls**.

The installed layout should be:

```text
Azimuth/
├── azimuth.exe
├── Start-Azimuth-Gamepad.exe
└── data/
    └── gamepad/
        ├── GamepadMod.dll
        ├── settings.ini
        └── presets/
            ├── 01-wasd.ini
            ├── 02-arrows.ini
            └── 03-smooth.ini
```

The emulator and game files are not included. No development tools are required to use the release build. When updating, keep your existing `data/gamepad/settings.ini` and files in `data/gamepad/presets/` to preserve your selected preset and key bindings.

### Controls and presets

Always launch the mod through **`Start-Azimuth-Gamepad.exe`**. The launcher locates the original `azimuth.exe` beside itself and starts it with the correct working directory, regardless of where the launcher was started. The original executable is not overwritten. The DLL, settings, log and editable configurations are stored in `data/gamepad/`.

The launcher menu gains a **Gamepad Controls ›** entry between **Game** and **Quit**:

- **Off** — keyboard control is disabled and the virtual gamepad is absent from Azimuth's device list. This is the initial state on a fresh installation.
- **Preset 1** — WASD: W accelerates, S brakes, A/D steer.
- **Preset 2** — arrow keys: Up accelerates, Down brakes, Left/Right steer.
- **Preset 3** — WASD with smooth steering and a maximum stick deflection of 75%.

Only one option can be active. The selection is saved in `settings.ini` and restored on the next launch, including **Off**. Switching between presets does not reconnect the device. **Off** removes the virtual gamepad while leaving physical gamepads available. All virtual buttons, axes and triggers are released when the window loses focus.

The controls panel is drawn inside Azimuth's menu, including fullscreen mode. Press **F9** in the main launcher menu to open it. Up/Down navigate Game, Gamepad Controls and Quit separately. Enter on Gamepad Controls opens the presets; Enter on Quit exits the application. Inside the panel, use the mouse, Up/Down, Enter or Escape. **Back** returns to the launcher menu. Input is captured while the panel is open so underlying menu entries, including Quit, are not activated.

### Editing presets

Configuration files are stored in `data/gamepad/presets/`:

| Menu option | File |
| --- | --- |
| Preset 1 | `presets/01-wasd.ini` |
| Preset 2 | `presets/02-arrows.ini` |
| Preset 3 | `presets/03-smooth.ini` |

Select a preset, then choose **Gamepad Controls → Edit preset config** to open its file in Notepad. Save your changes and choose **Reload configs**. When controls are off, the editor opens the first preset. Invalid configurations show an error and leave the previously loaded settings active.

`[buttons]` maps Xbox buttons, `[axes]` maps the negative and positive directions of both sticks, and `[triggers]` maps the left and right triggers.

Supported keys: `A`–`Z`, `0`–`9`, `SPACE`, `ENTER`, `BACKSPACE`, `TAB`, `ESC`, `SHIFT`, `CTRL`, `ALT`, `LSHIFT`, `RSHIFT`, `LCTRL`, `RCTRL`, `UP`, `DOWN`, `LEFT`, `RIGHT`, `HOME`, `END`, `PAGEUP`, `PAGEDOWN`, `INSERT`, `DELETE`, `NUMPAD0`–`NUMPAD9`. Use `NONE` to disable a binding. Letter bindings use Windows virtual key codes, so switching between Russian and English keyboard layouts does not require configuration changes.

The `[analog]` section supports:

- `stick_scale`: 0.05–1.0, maximum stick deflection.
- `trigger_scale`: 0.05–1.0, trigger strength.
- `steering_seconds`: 0–2.0, time to change horizontal steering by one unit; `0` makes steering instantaneous.

Configuration files use UTF-8; comments start with `;` or `#`. In `settings.ini`, `[mod] preset` stores the selection: `0` means Off and `1`–`3` select the corresponding preset. A missing or invalid value defaults to Off. A save failure displays an error and leaves the previous selection active. The runtime log is `data/gamepad/gamepad-mod.log`.

### How it works

The mod hooks the `RoGetActivationFactory` import only inside Azimuth and extends `Windows.Gaming.Input.Gamepad`. `LauncherMenu.cpp` inserts the controls entry into the GDI raster before presentation through BitBlt or GdiFlush. It intercepts PeekMessageW/GetMessageW before native input handling in run_launcher, handles mouse, pointer and keyboard messages, isolates native input polling, and moves Quit's hit coordinates together with its image.

No virtual controller driver is required. The virtual device is visible only to Azimuth started through the mod launcher; it is not exposed as a system Xbox controller to other applications. The emulator executable and game files are not modified.

Reverse-engineering code and the Ghidra database are kept separately in the development workspace's `reverse-engineering/` directory and are not needed to use the mod.

### Building and testing

Rebuilding requires Visual Studio 2022 with C++ x64 Build Tools and the Windows SDK. Place this repository's contents in a `source/` folder beside `azimuth.exe`, then run from the emulator root:

```bat
source\Build.cmd
```

The build runs configuration, selection persistence and input evaluation tests. The integration test checks DLL initialization, menu navigation, the initial Off state, all three presets, selection restoration in fresh processes, virtual gamepad enumeration and add/remove events, and neutral input outside the foreground window. Tests use temporary DLL and configuration copies in `data/gamepad/tests/profile-*` and do not modify your settings:

```bat
data\gamepad\tests\IntegrationTests.exe "C:\path\to\azimuth-windows\data\gamepad\GamepadMod.dll"
```

To test the launcher with a suspended test process:

```powershell
Start-Process .\Start-Azimuth-Gamepad.exe -ArgumentList '--self-test' -WindowStyle Hidden -Wait
Get-Content .\data\gamepad\tests\launcher-self-test.txt
```

The test also checks independent Game/Controls/Quit highlights, narrow native highlighting through GdiAlphaBlend, focus synchronization after native controller navigation, arrow stops in both directions, separate Enter actions, and queue interception before native hit-testing. It uses its own hidden window and does not launch a game. It covers the inserted entry and shifted Quit row, mouse and WM_POINTER input, keyboard navigation, native input polling suppression, and differing raster/window sizes.

The build preserves existing files in `data/gamepad/presets/`; templates from `source/presets/` are copied only when files are missing. Object files go to `data/gamepad/obj/`; test executables and reports go to `data/gamepad/tests/`. Test executables are generated by the source build and are not part of the installation package.

In-game behavior requires separate verification with installed game data. Set the game directory in `data/azimuth.ini` (`game=`) for your installation.

To run Azimuth without the mod, start the original `azimuth.exe`.

---

<a id="russian"></a>

## Русский
Мод клавиатурного управления для **Azimuth 0.9.10, Windows x64**.

### Установка

1. Скачайте архив готового мода на странице [Releases](https://github.com/Aleksey-Kon/Azimuth-Keyboard-Mod/releases).
2. Распакуйте содержимое папки мода **в корень эмулятора**, где находится `azimuth.exe`. Файл `Start-Azimuth-Gamepad.exe` должен оказаться рядом с `azimuth.exe`, а папка `data/gamepad/` — внутри папки `data` эмулятора. При копировании объедините папки `data`.
3. Запустите `Start-Azimuth-Gamepad.exe` и выберите пресет в **Gamepad Controls**.

После установки структура должна выглядеть так:

```text
Azimuth/
├── azimuth.exe
├── Start-Azimuth-Gamepad.exe
└── data/
    └── gamepad/
        ├── GamepadMod.dll
        ├── settings.ini
        └── presets/
            ├── 01-wasd.ini
            ├── 02-arrows.ini
            └── 03-smooth.ini
```

Сам эмулятор и игровые файлы в комплект мода не входят. Для готового мода установка инструментов разработки не требуется. При обновлении сохраните свои `data/gamepad/settings.ini` и файлы в `data/gamepad/presets/`, чтобы оставить выбранный пресет и назначения клавиш.

### Управление и пресеты

Запускайте **`Start-Azimuth-Gamepad.exe`**. Исходники и скрипт сборки находятся в `source/`. Загрузчик лежит рядом с `azimuth.exe`. DLL, настройки, журнал, редактируемые конфиги и остальные файлы сборки — в `data/gamepad/`. Загрузчик находит оригинальный `azimuth.exe` в корне папки эмулятора и запускает его с правильной рабочей папкой независимо от места запуска загрузчика. Оригинальный EXE не переписывается.

В собственном меню запуска Azimuth между **Game** и **Quit** появляется строка **«Gamepad Controls ›»**. Нажатие открывает раздел с вариантами:

- **Off** — управление выключено; виртуальный геймпад отсутствует в списке устройств Azimuth. Это состояние используется при первом запуске.
- **Preset 1** — WASD: W — газ, S — тормоз, A/D — руль.
- **Preset 2** — стрелки: вверх — газ, вниз — тормоз, влево/вправо — руль.
- **Preset 3** — WASD с плавным рулением и максимальным отклонением стика 75%.

Выбран может быть один вариант. Выбор сохраняется в `settings.ini` и восстанавливается при следующем запуске, включая «Off». Переключение между пресетами не переподключает устройство. «Off» убирает виртуальный геймпад и оставляет физические геймпады доступными. При потере фокуса окна все виртуальные кнопки, оси и триггеры отпускаются.

Раздел рисуется внутри меню Azimuth, включая полноэкранный режим. Его также можно открыть клавишей **F9** из главного меню запуска. В главном меню стрелки вверх/вниз отдельно выбирают Game, Gamepad Controls и Quit. Enter на Gamepad Controls открывает пресеты, Enter на Quit завершает работу. Внутри раздела работают мышь, стрелки вверх/вниз, Enter и Escape. «Back» возвращает в меню запуска. При открытом разделе ввод не передаётся старым пунктам меню, включая Quit.

### Редактирование

Конфиги расположены в `data/gamepad/presets/`:

| Пункт | Файл |
| --- | --- |
| Пресет 1 | `presets/01-wasd.ini` |
| Пресет 2 | `presets/02-arrows.ini` |
| Пресет 3 | `presets/03-smooth.ini` |

Выберите пресет, затем **«Gamepad Controls → Edit preset config»**. Конфиг откроется в Блокноте. После сохранения нажмите **«Reload configs»**. Когда управление выключено, команда редактирования открывает первый пресет. При ошибке в конфиге мод показывает сообщение и сохраняет ранее загруженные настройки.

`[buttons]` задаёт клавиши кнопок Xbox, `[axes]` — отрицательные и положительные направления обоих стиков, `[triggers]` — левый и правый триггеры.

Поддерживаются `A`–`Z`, `0`–`9`, `SPACE`, `ENTER`, `BACKSPACE`, `TAB`, `ESC`, `SHIFT`, `CTRL`, `ALT`, `LSHIFT`, `RSHIFT`, `LCTRL`, `RCTRL`, `UP`, `DOWN`, `LEFT`, `RIGHT`, `HOME`, `END`, `PAGEUP`, `PAGEDOWN`, `INSERT`, `DELETE`, `NUMPAD0`–`NUMPAD9`. `NONE` отключает назначение. Назначения букв используют физические виртуальные коды Windows, поэтому смена русской/английской раскладки не требует правки конфига.

В `[analog]`:

- `stick_scale`: 0.05–1.0, максимальное отклонение стиков.
- `trigger_scale`: 0.05–1.0, сила нажатия триггеров.
- `steering_seconds`: 0–2.0, время изменения горизонтального руления на одну единицу; `0` означает мгновенное изменение.

Кодировка конфигов — UTF-8; комментарии начинаются с `;` или `#`. В `settings.ini` ключ `[mod] preset` хранит выбор: `0` — Off, `1`–`3` — соответствующий пресет. Отсутствующее или некорректное значение означает Off. При ошибке записи показывается сообщение, а предыдущий выбор остаётся активным. Журнал работы — `data/gamepad/gamepad-mod.log`.

### Устройство мода

Мод подключается к импорту `RoGetActivationFactory` только внутри процесса Azimuth и расширяет `Windows.Gaming.Input.Gamepad`. Отдельный `LauncherMenu.cpp` добавляет строку в GDI-растр перед его показом через BitBlt или GdiFlush, перехватывает PeekMessageW/GetMessageW до штатной обработки ввода в run_launcher (она выполняется перед DispatchMessageW), обрабатывает сообщения мыши/указателя и клавиатуры, изолирует опрос ввода старым меню и переносит координаты Quit вместе с его изображением. Драйвер виртуального контроллера не требуется. Устройство видно только Azimuth, запущенному через загрузчик; оно не появляется как системный Xbox-контроллер в других приложениях. В код эмулятора на диске и игровые файлы изменения не вносятся.

Восстановленный код и база Ghidra хранятся отдельно в папке `reverse-engineering/` рабочего окружения разработчика. Они нужны для исследования и не используются при обычном запуске мода.

### Сборка и тесты

Для пересборки требуются Visual Studio 2022 с C++ x64 Build Tools и Windows SDK. Поместите содержимое репозитория в папку `source/` рядом с `azimuth.exe`, затем выполните из корня эмулятора:

```bat
source\Build.cmd
```

Сборка запускает тесты конфигов, сохранения выбора и вычисления ввода. Интеграционный тест проверяет подключение DLL, меню, начальное выключенное состояние, три пресета, восстановление выбора в новых процессах, добавление/удаление виртуального геймпада и нейтральный ввод вне фокуса. Тесты используют временные копии DLL и конфигов в `data/gamepad/tests/profile-*` и не изменяют пользовательский `settings.ini`:

```bat
data\gamepad\tests\IntegrationTests.exe "C:\путь\к\azimuth-windows\data\gamepad\GamepadMod.dll"
```

Полная проверка загрузчика с запуском приостановленного тестового процесса:

```powershell
Start-Process .\Start-Azimuth-Gamepad.exe -ArgumentList '--self-test' -WindowStyle Hidden -Wait
Get-Content .\data\gamepad\tests\launcher-self-test.txt
```

Тест также проверяет независимую подсветку Game/Controls/Quit, узкое штатное выделение через GdiAlphaBlend, синхронизацию фокуса после навигации штатным контроллером, остановки стрелок в обе стороны, разные действия Enter и ранний перехват очереди до штатного hit-test. Тест использует собственное скрытое окно и не запускает игру. Он проверяет отрисовку строки и смещение Quit, вход мышью и WM_POINTER, клавиатурное переключение, блокировку опроса старого меню и случай, когда размер растра отличается от размера окна.

Сборка сохраняет существующие пользовательские конфиги в `data/gamepad/presets/`. Шаблоны из `source/presets/` копируются только для отсутствующих файлов. Объектные файлы находятся в `data/gamepad/obj/`, тесты и результаты — в `data/gamepad/tests/`. Исполняемые файлы тестов создаются при сборке из исходников и не входят в установочный комплект.

Работа в самой игре требует отдельной проверки с установленными игровыми данными. Укажите игровую директорию для своей установки в `data/azimuth.ini` (`game=`).

Чтобы запустить Azimuth без мода, используйте обычный `azimuth.exe`.
