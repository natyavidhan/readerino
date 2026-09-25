// Temporary experiment: full-colour video from the ESP32's own flash on
// the 1.8" ST7735 TFT (no SD card). Build the video first:
//     python3 make_video.py <video>
// then compile with the Huge APP partition scheme (see README.md).
//
// Each frame is a baseline JPEG; JPEGDEC decodes it into a RAM frame
// buffer, which goes to the screen in one SPI burst. Frames are paced to
// the clip's frame rate; if one runs late the clock restarts from it rather
// than rushing the next ones. Loops forever and prints the real frame rate
// on the serial monitor (115200).

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <JPEGDEC.h>

// ---- wiring (ESP32 VSPI: SCK = GPIO18, MOSI = GPIO23) ----
#define TFT_CS 5
#define TFT_DC 21  // the display's "A0" pin
#define TFT_RST 22 // 21/22 exist on every DevKit (16/17 are missing on WROVER boards)
#define TFT_LED 4
#define SPI_HZ 27000000 // ST7735s usually take 40MHz too; drop to 16MHz if the picture is garbled

extern "C" const uint8_t video_start[]; // video.bin, embedded by video_blob.S

Adafruit_ST7735 tft(&SPI, TFT_CS, TFT_DC, TFT_RST);
JPEGDEC jpeg;

uint16_t vidW, vidH, fps;
uint32_t frameCount;
const uint32_t *offsets;
const uint8_t *jpegData;
uint16_t *frameBuf;

// JPEGDEC hands over the picture a block (MCU) at a time; copy each into
// the frame buffer, clipped to the picture (edge blocks can overhang it).
int copyBlock(JPEGDRAW *d) {
  for (int row = 0; row < d->iHeight && d->y + row < vidH; row++) {
    int w = min(d->iWidth, (int)vidW - d->x);
    if (w > 0) memcpy(&frameBuf[(d->y + row) * vidW + d->x], &d->pPixels[row * d->iWidth], w * 2);
  }
  return 1;
}

void setup() {
  Serial.begin(115200);
  pinMode(TFT_LED, OUTPUT);
  digitalWrite(TFT_LED, HIGH);

  tft.initR(INITR_BLACKTAB);
  tft.setSPISpeed(SPI_HZ);
  tft.setRotation(1); // landscape, same orientation as readerino_nano
  tft.fillScreen(ST77XX_BLACK);

  if (memcmp(video_start, "MJV1", 4) != 0) {
    Serial.println("video.bin missing or bad -- run make_video.py");
    tft.setTextColor(ST77XX_RED);
    tft.print("no video.bin");
    while (true) delay(1000);
  }
  vidW = *(const uint16_t *)(video_start + 4);
  vidH = *(const uint16_t *)(video_start + 6);
  fps = *(const uint16_t *)(video_start + 8);
  frameCount = *(const uint32_t *)(video_start + 12);
  offsets = (const uint32_t *)(video_start + 16);
  jpegData = video_start + 16 + 4 * (frameCount + 1);
  frameBuf = (uint16_t *)malloc(vidW * vidH * 2);
  Serial.printf("video %ux%u @ %ufps, %u frames\n", vidW, vidH, fps, frameCount);
}

void loop() {
  const uint32_t periodUs = 1000000UL / fps;
  const int x0 = (tft.width() - vidW) / 2, y0 = (tft.height() - vidH) / 2;
  uint32_t due = micros(), start = millis(), worstUs = 0, late = 0;

  for (uint32_t i = 0; i < frameCount; i++) {
    uint32_t t0 = micros();
    if (jpeg.openRAM((uint8_t *)(jpegData + offsets[i]), offsets[i + 1] - offsets[i], copyBlock)) {
      jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
      jpeg.decode(0, 0, 0);
      jpeg.close();
    }
    tft.startWrite();
    tft.setAddrWindow(x0, y0, vidW, vidH);
    tft.writePixels(frameBuf, vidW * vidH);
    tft.endWrite();
    uint32_t took = micros() - t0;
    if (took > worstUs) worstUs = took;

    due += periodUs;
    int32_t wait = (int32_t)(due - micros());
    if (wait > 0) delayMicroseconds(wait);
    else { due = micros(); late++; } // ran late: carry on from now, don't rush
  }

  float secs = (millis() - start) / 1000.0f;
  Serial.printf("loop: %u frames in %.2fs = %.1f fps (target %u), worst frame %.1f ms, %u late\n",
                frameCount, secs, frameCount / secs, fps, worstUs / 1000.0f, late);
}
