# ESP Secure Home

A standalone home security controller built on the ESP32S3 using ESP IDF. It watches a door (reed switch) and an equipment enclosure (tamper switch), shows status on a 16x2 LCD, accepts admin commands from a 4x4 matrix keypad, and sends Telegram alerts to a configurable list of recipients when an intrusion is detected while the system is not in admin mode. All configuration, including WiFi credentials, the admin password and the Telegram recipient list, is entered live through the keypad and persisted to flash, so no source changes or reflashing are needed after the bot token is set once.

## Hardware

* ESP32S3 module (16 MB flash, octal PSRAM as configured in `sdkconfig.defaults`)
* HD44780 compatible 16x2 character LCD behind a PCF8574 I2C backpack at address `0x27`
* 4x4 matrix keypad (digits 0 to 9, letters A to D, star and pound)
* A reed switch on the door, wired normally closed
* A tamper switch on the enclosure, also wired normally closed

Pin assignments live in `config.hpp`:

* LCD: SDA on GPIO1, SCL on GPIO2, I2C port 0
* Keypad rows: GPIO6, GPIO7, GPIO15, GPIO16
* Keypad columns: GPIO3, GPIO9, GPIO10, GPIO11
* Reed switch: GPIO12
* Tamper switch: GPIO13

Both switches are normally closed loops. The firmware treats a rising edge on either pin as the loop having been broken, whether that means the door was opened or the enclosure was pried open.

## How the system behaves

On boot the LCD shows a short startup sequence, then settles on a password prompt. From here the device is in locked mode: any door or tamper event is treated as an intrusion, gets logged, is shown briefly on the LCD, and triggers a Telegram message to every registered recipient, provided WiFi is connected.

Entering the correct eight digit admin password switches the device into admin mode and opens a menu with five entries:

1. View numbers, to scroll through registered Telegram chat IDs
2. Add number, to register a new ten digit chat ID
3. Remove number, to scroll through and delete a registered chat ID
4. Change password, which asks for the new password twice to confirm it
5. WiFi setup, which scans for networks, lets you pick one, and asks for a password if the network is not open

While in admin mode, door and tamper events are only logged and shown on the LCD, not reported over Telegram, since opening the enclosure or the door is expected behavior during setup or maintenance.

Keypad controls follow a fixed convention throughout the menus:

* A confirms or selects
* B is backspace
* C scrolls up, or toggles uppercase and lowercase while typing a WiFi password
* D scrolls down
* Star cancels the current entry or goes back one level
* Pound logs out immediately and returns to the password prompt from anywhere in admin mode

Five consecutive wrong password attempts lock the keypad for sixty seconds. Ten minutes of inactivity while in admin mode logs the session out automatically. Both limits are set in `config.hpp`.

WiFi passwords are typed using multi tap text entry, the same scheme used on old T9 phones: pressing a digit key repeatedly cycles through its letters, digits and a small set of punctuation marks, and the character locks in once a one second window passes without a repeat press of the same key. This exists because WPA2 passwords need a broader character set than the twelve digit keys alone provide.

The default admin password on first boot is `12345678`. It should be changed through the admin menu before the device should be used.

## Storage layout

Data is kept in three flat files on a LittleFS partition mounted at `/lfs/storage`, one each for the password, the recipient list and the WiFi credentials. Each file is a fixed size raw binary image of a small struct, always fully rewritten with `fseek` to the start, `fwrite`, `fflush` and `fsync` on every change, so a write either fully lands or the previous contents are left untouched on disk.

A sentinel file at `/lfs/sentinel.txt` marks whether this is the first boot. When it is missing, the storage directory and the three data files are created with defaults (default password, an empty recipient list, no saved WiFi credentials), the sentinel file is written, and the device reboots into normal startup with those defaults now in place.

Every write to storage keeps a copy of the previous in memory value and rolls back to it if the corresponding flash write fails, so a failed write cannot leave the in memory state and the on disk state disagreeing with each other.

## WiFi behavior

At boot, if credentials were saved from a previous session, the device attempts to connect automatically. The ESP IDF WiFi event handler reconnects on its own whenever the connection drops, as long as credentials are on file. A background task also checks the connection state periodically and reboots the device if it has been unable to reconnect for too long, as a backstop in case the automatic reconnect logic gets stuck.

Successful connections made through the WiFi setup menu are saved to flash immediately, so they carry over to the next boot without needing to be re-entered.

## Telegram alerts

Alerts are sent through the Telegram Bot API's `sendMessage` endpoint over HTTPS using ESP IDF's HTTP client with the built in certificate bundle. The bot token lives in `config::TELEGRAM_BOT_TOKEN` in `config.hpp` and needs to be set before building. Recipients are identified by their numeric Telegram chat ID, which must be obtained separately, for example by messaging the bot and reading the chat ID it receives back, or through a helper bot such as `@userinfobot`.

If WiFi is not connected when an intrusion is detected, the alert is dropped and only logged locally, since there is nowhere to send it.

## Building and flashing

This project targets ESP-IDF v6.0.1:

```
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

Component dependencies are declared in `idf_component.yml` and pinned in `dependencies.lock`. They are fetched automatically by `idf.py build` through the ESP IDF Component Registry and do not need to be installed separately:

* `esp-idf-lib/hd44780` for driving the character LCD
* `esp-idf-lib/pcf8574` for the I2C GPIO expander the LCD sits behind
* `esp-idf-lib/esp_idf_lib_helpers` as a shared dependency of the two above
* `joltwallet/littlefs` for the flash filesystem used by the storage layer

The partition table in `partitions.csv` reserves 2 MB for the application image and a separate 2 MB LittleFS partition named `storage`, alongside the usual NVS and PHY init partitions. It assumes a 16 MB flash chip, matching `CONFIG_ESPTOOLPY_FLASHSIZE_16MB` in `sdkconfig.defaults`.

Before the first build, set `TELEGRAM_BOT_TOKEN` in `config.hpp` to a real bot token. Everything else, including the admin password, the WiFi network and the recipient list, is configured afterwards from the keypad.

### Static analysis and formatting

The top level `CMakeLists.txt` adds two optional build targets when the corresponding tools are found on the system:

```
idf.py cppcheck
idf.py format
```

`cppcheck` runs static analysis over the application sources using the project's compile commands and a suppressions file. `format` runs `clang-format` over every tracked C, C++, and header file according to the style in the `.clang-format` file.

## Code structure

`config.hpp` centralizes every pin assignment, timing constant, task stack size and priority, and the Telegram bot token, so hardware and behavior tuning happens in one place.

`utils.hpp` holds small shared helpers used across the codebase: the `TRY` family of macros for propagating `esp_err_t` failures with logging, a `consteval` string concatenation helper used to build file paths and URLs at compile time, a helper for building compile time filled arrays, and `fatal`/`reboot` wrappers that log before halting or restarting the system.

`file.hpp` and `file.cpp` are the lowest storage layer: opening, creating, reading from and writing to the three fixed files on LittleFS, along with the first boot sentinel check. Every write is followed by a flush and an `fsync` so data survives a power loss immediately after a change.

`storage.hpp` and `storage.cpp` sit above `file` and own the typed, in memory representation of the password, the recipient list, and the WiFi credentials. They expose operations like checking a password, changing it, adding or removing a recipient, and reading or writing WiFi credentials, each of which keeps the file layer and the in memory copy consistent and rolls back on a failed write.

`wifi.hpp` and `wifi.cpp` wrap the ESP IDF WiFi station APIs: initialization, scanning, connecting, remembering the last used credentials for automatic reconnection, and reporting connection state through an event group.

`telegram.hpp` and `telegram.cpp` implement sending a single message to a single chat ID through the Telegram Bot API.

`display.hpp` and `display.cpp` are the thin driver layer over the HD44780 LCD: clearing lines or the whole screen, writing a single character at a position, writing a padded line of text, controlling the backlight, and the canned boot up and shutdown sequences.

`screen.hpp` defines the canned, statically known screens (the password prompt and the four switch broken alerts) as a lookup table of two line messages, along with the `display_request_t` structure used to pass a screen, or free form custom text, through a queue to the display task. `ui_helpers.hpp` builds on top of this to offer higher level helpers for the admin UI: rendering the menu, a masked password prompt, a recipient entry, or a WiFi network entry, all of which end up as a `display_request_t` pushed onto the same queue.

`keypad.hpp` implements the interrupt driven, debounced matrix keypad scanner as a template class, pushing individual key characters onto a queue as they are detected.

`multitap.hpp` implements the T9 style multi tap text entry engine used only for WiFi passwords, tracking which key was last pressed, the current cycle position within that key's character set, whether the previous character is still open to being cycled, and the caps lock state.

`switch.hpp` implements the interrupt driven, debounced reed and tamper switch detectors as a template class parameterized on which switch type it is, notifying a receiving task once a break is confirmed past the debounce timer.

`system.hpp` and `system.cpp` provide the small glue layer used by the switch task: writing to the LCD through `display` with a consecutive failure counter that reboots the device if the LCD stops responding, and deciding whether a switch break should only be logged (admin mode) or should also trigger a Telegram alert to every registered recipient (normal mode).

`tasks.hpp` and `tasks.cpp` tie everything together. `tasks::run()` initializes every subsystem (filesystem, storage, WiFi, GPIO interrupt service, display, the display request queue) and starts four FreeRTOS tasks:

* The display task owns the LCD exclusively, reading `display_request_t` values off a queue and rendering them, including holding a transient message for its configured duration before reverting to whatever screen was displayed persistently before it.
* The switch task owns the reed and tamper switch drivers, forwarding confirmed breaks to the display queue and to `sys::alert_on_*` for logging and Telegram alerts.
* The WiFi task attempts a connection using any saved credentials on boot, then periodically checks the connection is still alive as a backstop against the event driven reconnect logic getting stuck.
* The system task owns the keypad and the entire admin UI state machine: password entry, lockout, the admin menu, viewing and managing recipients, changing the password, and the WiFi setup flow including network scanning, selection and multi tap password entry.

Only the display task ever writes to the LCD, and only the system task ever reads the keypad, so there is no shared mutable UI or input state across tasks; everything crosses task boundaries through the display queue or through the storage and WiFi modules, which manage their own internal state safely.
