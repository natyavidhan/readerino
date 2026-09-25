#pragma once
#include <Arduino.h>

// Plays .rvd videos and shows .rim images (formats: packer/media.py).
//
// Nothing is ever loaded whole: a video is streamed from the card through
// a small read-ahead buffer (lent by the caller -- the app reuses memory
// the bookmarks screen doesn't need while something plays), and every
// frame is decoded straight onto the TFT, whose own memory is the frame
// buffer. prefetch() tops the buffer up during the idle time between
// frames, so a heavy frame's data is usually already in RAM when it's due.
namespace Media {
  // ---- video ----
  bool openVideo(const char *path, uint8_t *buffer, uint8_t bufferSize);
  void close();

  uint32_t frameCount();
  uint8_t fps();
  uint16_t keyInterval(); // frames per keyframe (= one second)
  uint16_t keyCount();
  uint32_t nextFrame(); // index of the frame drawNextFrame() will draw

  // Draws keyframe k in full; playback then continues from frame
  // k * keyInterval() + 1.
  bool seekKey(uint16_t k);
  // Draws the next frame; false once the video has ended (or on a read error).
  bool drawNextFrame();
  // Fills the read-ahead buffer from the card; call while waiting for the
  // next frame to be due.
  void prefetch();

  // ---- image ----
  // Draws a .rim image centered on a black screen, streaming it from the
  // card through the buffer.
  bool drawImage(const char *path, uint8_t *buffer, uint8_t bufferSize);
}
