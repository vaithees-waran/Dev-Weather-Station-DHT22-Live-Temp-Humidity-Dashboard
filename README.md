# LinkX Weather Station — DHT22 Live Temp/Humidity Dashboard

## Objective
Display live temperature and humidity with a rolling trend chart, log a reading every 5 minutes to LittleFS, and give at-a-glance comfort-zone/alarm feedback via RGB and buzzer — fully offline, no cloud or CDN dependency.

## Real-World Applications
Home/room climate monitoring, greenhouse/server-room temperature logging, classroom weather-station projects.

## Hardware & Pin Mapping
| Component | Pin | Notes |
|---|---|---|
| RGB, Buzzer | built-in | |
| DHT22 DATA | GPIO35 | add a 10k pull-up to 3.3V if not already on your module |

## Wiring
| DHT22 Pin | ESP32-S3 Pin |
|---|---|
| VCC | 3.3V |
| GND | GND |
| DATA | GPIO35 (+10k pull-up to 3.3V if needed) |

## Why No Chart.js CDN
Most AP-only setups mean the phone has no real internet while connected to the robot's WiFi, so a Chart.js `<script src="https://cdn...">` tag would just fail to load. This dashboard draws its trend chart with a small vanilla-JS `<canvas>` routine instead — fully self-contained, works with zero internet access.

## Dashboard
- Live temperature (°C) and humidity (%) tiles
- Rolling ~5-minute trend chart (red=temperature, blue=humidity)
- Data log card with a "Download CSV" link (the 5-minute interval log, separate from the live chart)
- Event log (sensor errors, alarms, logging confirmations)

## Comfort-Zone RGB Logic
| Condition | Color |
|---|---|
| Temp < 15°C | Blue (cold) |
| 15–28°C | Green (comfortable) |
| Temp > 28°C | Orange/red (hot) |
| 3+ consecutive sensor read failures | Amber (sensor error) |
| Boot | Purple |

## Alarm Logic
Buzzer triple-beeps and a dashboard alert fires if temperature exceeds 38°C or humidity exceeds 88% — tune `ALARM_TEMP_C`/`ALARM_HUMIDITY` at the top of the sketch for your climate.

## Sensor Calibration & Error Handling
DHT22 doesn't need field calibration, but note:
- It needs **at least 2 seconds** between reads — the firmware polls every 3s to stay safely above that
- A single bad read (common, even on good wiring) is ignored silently; only **3 consecutive** failures trigger the dashboard error state, avoiding false alarms
- The last known-good reading stays displayed during a glitch instead of showing `NaN`/garbage

## Data Logging & OTA
- `/log.csv` — general events (errors, alarms, logging confirmations)
- `/weather_log.csv` — one row every 5 minutes: `millis,temp_c,humidity_pct`, rotates at 500KB
- OTA via `ArduinoOTA` hostname `linkx-weatherstation`

## Testing Procedure
1. Wire the DHT22, flash + upload `/data`, power on
2. Confirm Serial shows a valid first reading (not NaN) within a few seconds of boot
3. Open the dashboard — confirm live temp/humidity update every ~3s and the chart starts drawing
4. Breathe warm/humid air near the sensor — confirm both values rise and the chart reflects it
5. Wait 5 minutes — confirm a "Logged reading" event appears and `/weather_log.csv` has a new row
6. Disconnect the DHT22 DATA wire briefly — confirm 3 consecutive failures trigger the amber error state, then reconnect and confirm recovery is logged

## Folder Structure
```
weather_station/
├── weather_station.ino
├── data/
│   └── index.html
└── README.md
```

## Libraries
`ESPAsyncWebServer`, `AsyncTCP`, `ArduinoJson`, `LittleFS`, `ArduinoOTA` (bundled) + **`DHT sensor library` by Adafruit** and its dependency **`Adafruit Unified Sensor`** (both install via Library Manager).

## Future Improvements
- Add a min/max/average daily summary computed from `/weather_log.csv`
- Add NTP time sync (when STA WiFi is configured) to log real timestamps instead of `millis()` uptime
- Add a heat-index / dew-point calculation alongside raw temp/humidity
