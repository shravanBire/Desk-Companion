# Desk Companion

A small ESP32-powered desktop companion that combines a network-synchronized clock, a persistent stopwatch, an animated OLED display, and synchronized LED effects in a compact desk gadget.

The project is designed around a single-button interface and a 128×64 I2C OLED. When the stopwatch is running without interaction, the display automatically transitions into an animated scene of two rabbits hopping toward each other while the LEDs pulse with the animation.

## Features

- **Wi-Fi time synchronization** using NTP.
- **Real-time clock display** in `HH:MM:SS` format.
- **Stopwatch** with start, pause, resume, and reset controls.
- **Persistent stopwatch state** stored in ESP32 non-volatile storage.
- **Automatic animation mode** after a period of stopwatch activity without interaction.
- **Two-rabbit OLED animation** with hopping, blinking, ear-touch, and idle phases.
- **PWM LED effects** synchronized with the animation brightness.
- **Single-button controls** with debouncing, short press, double press, and long press detection.
- **Offline fallback**: if Wi-Fi connection fails, the device continues running using the ESP32's boot-time clock state.

## Hardware

### Required components

- ESP32 development board
- 0.96-inch SSD1306 OLED display, 128×64, I2C
- Momentary push button
- 2 LEDs
- 2 current-limiting resistors, approximately 220 Ω
- USB cable or suitable power source
- Jumper wires and a breadboard or custom enclosure

### Wiring

| Component | Connection |
|---|---|
| OLED VCC | ESP32 3V3 |
| OLED GND | ESP32 GND |
| OLED SDA | GPIO 21 |
| OLED SCL | GPIO 22 |
| Button leg 1 | GPIO 4 |
| Button leg 2 | GND |
| Left LED anode | GPIO 13 through approximately 220 Ω resistor |
| Left LED cathode | GND |
| Right LED anode | GPIO 12 through approximately 220 Ω resistor |
| Right LED cathode | GND |

The button uses the ESP32's internal pull-up resistor, so an external pull-up resistor is not required.

> **Important:** The current firmware definitions use GPIO 13 for the left LED and GPIO 12 for the right LED. If you use a different wiring layout, update `LED_LEFT_PIN` and `LED_RIGHT_PIN` in `desk_Companion.ino`.

### Power

Power the ESP32 through USB during development. For battery-powered operation, connect the battery only according to the specifications of your particular ESP32 board and regulator. Do not connect an unknown battery directly to the 3V3 pin.

## Software requirements

- Arduino IDE 2.x or Arduino-compatible build environment
- ESP32 board support package for Arduino
- A USB data cable
- The following Arduino libraries:
  - `WiFi.h` — included with the ESP32 board package
  - `time.h` — included with the ESP32 board package
  - `Wire.h` — included with Arduino/ESP32 support
  - `Preferences.h` — included with the ESP32 board package
  - [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library)
  - [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306)

## Getting started

### 1. Install the ESP32 board package

In Arduino IDE:

1. Open **File → Preferences**.
2. Add the Espressif ESP32 boards URL to **Additional Boards Manager URLs**:

   ```text
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```

3. Open **Tools → Board → Boards Manager**.
4. Search for **esp32** and install the package published by Espressif Systems.
5. Select the board that matches your hardware, such as **ESP32 Dev Module**.

### 2. Install the display libraries

Open **Sketch → Include Library → Manage Libraries**, then install:

- **Adafruit GFX Library**
- **Adafruit SSD1306**

### 3. Configure Wi-Fi and timezone

Open `desk_Companion.ino` and update the configuration near the top of the file:

```cpp
const char* ssid       = "YOUR_WIFI_NAME";
const char* password   = "YOUR_WIFI_PASSWORD";
const char* ntpServer  = "pool.ntp.org";
const long  gmtOffsetSec = 19800; // UTC+05:30
const int   daylightOffsetSec = 0;
```

For another timezone, replace `gmtOffsetSec` with the appropriate UTC offset in seconds. For example:

- UTC: `0`
- UTC−05:00: `-18000`
- UTC+01:00: `3600`
- UTC+05:30: `19800`

The current code is configured for India Standard Time (IST), UTC+05:30.

> **Security note:** Do not commit real Wi-Fi credentials to a public repository. For a public project, consider loading credentials from a separate local configuration file or provisioning them at runtime.

### 4. Connect the hardware

Wire the ESP32, OLED, button, and LEDs according to the wiring table above. Confirm the OLED I2C address is `0x3C`, which is the address used by the firmware. Some displays use `0x3D`; if yours does, update this line:

```cpp
display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
```

### 5. Upload the firmware

1. Connect the ESP32 to your computer.
2. Select the correct board under **Tools → Board**.
3. Select the correct port under **Tools → Port**.
4. Open `desk_Companion.ino`.
5. Click **Verify** to compile the sketch.
6. Click **Upload**.
7. Open **Tools → Serial Monitor** at `115200` baud for diagnostic messages.

On startup, the device initializes the OLED, loads the saved stopwatch value, connects to Wi-Fi, and attempts to synchronize the time using NTP.

## Controls

The button is interpreted using software debouncing and press-duration detection.

| Action | Behavior |
|---|---|
| Short press | Switch between real-time clock and numeric stopwatch views. From animation mode, return to the numeric stopwatch. |
| Long press, at least 600 ms | Start or resume the stopwatch when stopped; pause and save it when running. |
| Double press | Reset the stopwatch to `00:00:00` while in numeric or animation mode. |
| No interaction while stopwatch is running | After 10 seconds, switch from numeric stopwatch view to animation mode. |

The stopwatch value is periodically saved while running and is also saved when paused or reset. After a reboot, the saved value is restored, but the stopwatch always starts in a paused state.

## Display modes

### Real-time clock

The OLED shows the current local time in `HH:MM:SS` format after a successful time synchronization. If time retrieval is unavailable, it displays `Time unavailable`.

### Numeric stopwatch

The stopwatch displays elapsed time as `HH:MM:SS`. The stopwatch can be paused and resumed without losing the accumulated value.

### Rabbit animation

The animation consists of four phases:

1. Two rabbits hop toward the center.
2. They meet and briefly touch ears.
3. They hop back to their home positions.
4. They idle before the cycle repeats.

The two LEDs mirror the animation energy: dim during low movement, brighter at hop peaks, and brightest during the ear-touch moment.

## How it works

The firmware is organized into a few main responsibilities:

- **Initialization:** configures GPIO, PWM channels, OLED, preferences storage, and Wi-Fi time synchronization.
- **Button state machine:** debounces the input and distinguishes short, long, and double presses.
- **Stopwatch logic:** tracks elapsed time using `millis()` and persists the value with `Preferences`.
- **Rendering:** draws the clock, stopwatch, or animation frame to the OLED.
- **LED synchronization:** updates both LED brightness values based on the current animation frame.
- **Main loop:** processes input, handles automatic view transitions, saves state periodically, renders the active view, updates LEDs, and repeats every approximately 20 ms.

## Troubleshooting

### OLED initialization failed

- Confirm VCC and GND connections.
- Confirm SDA is connected to GPIO 21 and SCL to GPIO 22.
- Check whether your display uses I2C address `0x3C` or `0x3D`.
- Make sure the Adafruit GFX and Adafruit SSD1306 libraries are installed.

### Wi-Fi does not connect

- Verify the SSID and password in `desk_Companion.ino`.
- Use a 2.4 GHz network if your ESP32 board does not support 5 GHz.
- Keep the device within range of the access point.
- Check the Serial Monitor at 115200 baud.
- The device will still start if Wi-Fi fails, but network time synchronization will not be available.

### LEDs do not light

- Check LED polarity.
- Confirm the resistor is in series with each LED.
- Confirm the LED pins match the firmware: GPIO 13 and GPIO 12.
- Avoid powering LEDs directly from a pin without a current-limiting resistor.

### Stopwatch value is not retained

- Make sure the ESP32 has enough stable power to complete the save operation.
- Remember that persistence is stored on the ESP32's internal NVS, not in the source code.
- The value is restored after reboot, but the stopwatch intentionally starts paused.

### Upload fails

- Select the correct board and serial port.
- Try a different USB data cable.
- Hold the board's **BOOT** button during upload if your board requires it.
- Close other applications using the serial port.

## Project structure

```text
Desk-Companion/
├── desk_Companion.ino  # Complete ESP32 Arduino firmware
└── README.md            # Project documentation
```

## Customization

You can adapt the project by changing:

- `LED_LEFT_PIN` and `LED_RIGHT_PIN` for different LED wiring.
- `BUTTON_PIN` for a different button input.
- `gmtOffsetSec` and `daylightOffsetSec` for your timezone.
- `ANIMATION_TIMEOUT_MS` to change when animation mode starts.
- `LONG_PRESS_MS` and `DOUBLE_PRESS_WINDOW_MS` to adjust button behavior.
- `APPROACH`, `MEET`, `RETREAT`, and `PAUSE` to change the animation timing.
- Rabbit positions and drawing parameters in `drawRabbitShape()` and `drawTwoRabbits()`.

## Safety and hardware notes

- Always use current-limiting resistors with discrete LEDs.
- Check your ESP32 board's voltage regulator and battery requirements before using external power.
- Avoid shorting GPIO pins or applying 5 V to 3.3 V-only pins.
- Disconnect power before changing wiring.
- This project is intended for hobby and educational use.

## Contributing

Contributions are welcome. To propose an improvement:

1. Fork the repository.
2. Create a feature branch.
3. Make and test your changes on real hardware.
4. Update the documentation if behavior or wiring changes.
5. Open a pull request with a clear description and test notes.

## License

No license has currently been declared for this repository. Unless a license is added, the default copyright rules apply and reuse, modification, or redistribution may be restricted. Add a license file if you want others to use or modify the project under explicit terms.

## Author

Created by [shravanBire](https://github.com/shravanBire).

Repository: [github.com/shravanBire/Desk-Companion](https://github.com/shravanBire/Desk-Companion)
