# 🌸 PomiPomi
## Hi, I'm PomiPomi! Let's get _pomictive_! 🌸 
A tiny pixel-art desk companion: a home clock face, a Pomodoro timer, and a quick countdown timer, all on a small SPI TFT display driven by an ESP32.

## Hardware

| Component | Detail |
|---|---|
| Microcontroller | ESP32 (Arduino core) — uses `WiFi.h` for networking |
| Display | SPI TFT, 320×240, landscape (`setRotation(1)`), driven via the [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) library. The exact driver chip/timings are configured in TFT_eSPI's own `User_Setup.h`, not in this sketch. |
| Rotary encoder | KY-040 style — `CLK` → GPIO34, `DT` → GPIO35, `SW` (push button) → GPIO32 |
| Buzzer | Active buzzer module on GPIO25, low-level trigger (`digitalWrite(BUZZ_PIN, LOW)` = beep, `HIGH` = silent) |
| Network | Onboard WiFi, used for NTP time sync (`pool.ntp.org`) |

## Setup

1. Copy `arduino_secrets.h.example` to `arduino_secrets.h` and fill in your WiFi credentials:
   ```cpp
   #define SECRET_SSID "your-wifi-name"
   #define SECRET_PASSWORD "your-wifi-password"
   ```
   `arduino_secrets.h` is gitignored so your credentials never get committed.
2. Configure `TFT_eSPI`'s `User_Setup.h` for your specific display driver/pinout.
3. Install libraries: `TFT_eSPI`, `NTPClient`.
4. Flash `sketch_jul1a.ino` to the ESP32.

## Controls

- **Home screen**: shows the clock face, current time, and date.
  - Double-click → quick countdown timer (set minutes/seconds, then it counts down)
  - Triple-click → Pomodoro mode
  - Long-press → set the clock manually (overrides NTP until the board resets)
- **Pomodoro mode**: work/short-break/long-break cycles with session tracking.
  - Press → start / pause / resume
  - Long-press (from idle) → edit work/short/long break durations
  - Triple-click → back to home screen
  - Auto-returns to home screen after 15s idle on the Pomodoro menu
- **Countdown timer**: rotate to set minutes → press → rotate to set seconds → press to start.
  - Press → pause / resume
  - Triple-click → back to home screen
