# ESP32 video experiment

Full-colour video on the 1.8" ST7735, stored in the ESP32's own flash (no
SD card): MJPEG frames decoded with JPEGDEC into a RAM frame buffer and
pushed over hardware SPI, paced to the clip's frame rate.

## Wiring (ESP32 DevKit)

| TFT pin | ESP32 |
|---|---|
| VCC | 5V (VIN) — the module has its own 3.3V regulator |
| GND | GND |
| SCK | GPIO18 |
| SDA | GPIO23 |
| CS | GPIO5 |
| A0 | GPIO21 |
| RESET | GPIO22 |
| LED | GPIO4 |

All 3.3V logic — no resistor dividers.

## Build and flash

```bash
arduino-cli lib install JPEGDEC   # plus Adafruit ST7735 and ST7789 Library, Adafruit GFX
python3 make_video.py path/to/clip.mp4           # writes video.bin + video_blob.S (gitignored)
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app .
arduino-cli upload -p /dev/ttyUSBx --fqbn esp32:esp32:esp32:PartitionScheme=huge_app .
```

`huge_app` gives the program ~3MB of a 4MB flash; `make_video.py` picks
the best JPEG quality that fits `--budget-mb` (default 2.4MB). The serial
monitor (115200) prints the real frame rate after every loop.
