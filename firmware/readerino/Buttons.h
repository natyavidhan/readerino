#pragma once
#include <Arduino.h>

enum class ButtonEvent {
  None,
  UpPressed,
  DownPressed,
  SelectShort,
  SelectLong,
};

namespace Buttons {
  void begin();
  ButtonEvent poll(); // call every loop(); returns at most one event per call
}
