#pragma once

// A trimmed version of the ESP32 build's wire protocol: HELLO/PUT/DELETE/
// LIST/BYE only. GET/SETPOS/ADDBM (verification/debug commands on the
// ESP32 build) were cut here purely for flash budget — this chip's 32KB
// leaves no room to spare. Re-implemented without String/std::vector: RAM
// is the binding constraint, so every buffer is a fixed, stack-local array
// sized just large enough for its job.
namespace Transfer {
  void begin();
  bool poll(); // call every loop(); returns true if it consumed a full session
}
