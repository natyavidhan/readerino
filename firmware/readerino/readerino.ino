#include "App.h"
#include "Transfer.h"

void setup() {
  Transfer::begin();
  App::begin();
}

void loop() {
  if (Transfer::poll()) return; // a host-driven file transfer consumed this tick
  App::loop();
}
