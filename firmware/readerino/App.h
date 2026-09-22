#pragma once

namespace App {
  void begin();
  void loop();
  // Call after a host file transfer so newly added/removed books show up
  // without needing a reboot. Cheap no-op cost-wise except right after a
  // transfer: it re-scans the SD card and, if the menu is on screen,
  // redraws it.
  void onFilesChanged();
}
