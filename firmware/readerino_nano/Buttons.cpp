#include "Buttons.h"
#include "Config.h"

namespace {
  struct DebouncedButton {
    uint8_t pin;
    bool stableState;   // HIGH = released (INPUT_PULLUP)
    bool lastReading;
    unsigned long lastChangeMs;
    DebouncedButton(uint8_t p) : pin(p), stableState(true), lastReading(true), lastChangeMs(0) {}
  };

  DebouncedButton btnUp{PIN_BTN_UP};
  DebouncedButton btnDown{PIN_BTN_DOWN};
  DebouncedButton btnSelect{PIN_BTN_SELECT};

  unsigned long selectPressStartMs = 0;
  bool selectLongFired = false;

  void updateDebounce(DebouncedButton &b, bool &pressedEdge, bool &releasedEdge) {
    pressedEdge = false;
    releasedEdge = false;
    bool reading = digitalRead(b.pin); // HIGH = released, LOW = pressed
    if (reading != b.lastReading) {
      b.lastChangeMs = millis();
      b.lastReading = reading;
    }
    if ((millis() - b.lastChangeMs) > DEBOUNCE_MS && reading != b.stableState) {
      b.stableState = reading;
      if (reading == LOW) pressedEdge = true;
      else releasedEdge = true;
    }
  }
}

void Buttons::begin() {
  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
  pinMode(PIN_BTN_SELECT, INPUT_PULLUP);
}

ButtonEvent Buttons::poll() {
  bool pressed, released;

  updateDebounce(btnUp, pressed, released);
  if (pressed) return ButtonEvent::UpPressed;

  updateDebounce(btnDown, pressed, released);
  if (pressed) return ButtonEvent::DownPressed;

  updateDebounce(btnSelect, pressed, released);
  if (pressed) {
    selectPressStartMs = millis();
    selectLongFired = false;
  }
  if (btnSelect.stableState == LOW && !selectLongFired &&
      (millis() - selectPressStartMs) >= LONG_PRESS_MS) {
    selectLongFired = true;
    return ButtonEvent::SelectLong;
  }
  if (released && !selectLongFired) {
    return ButtonEvent::SelectShort;
  }

  return ButtonEvent::None;
}
