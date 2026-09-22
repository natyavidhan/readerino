#include "App.h"
#include "Transfer.h"

void setup() {
  Transfer::begin();
  App::begin();
}

void loop() {
  if (Transfer::poll()) {
    App::onFilesChanged(); // pick up any books the transfer just added/removed
    return;
  }
  App::loop();
}
