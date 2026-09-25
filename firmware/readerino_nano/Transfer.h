#pragma once

// Serial file transfer protocol used by packer/push_to_sd.py: HELLO, then
// PUT/GET/DELETE/LIST, then BYE. Kept minimal for flash, and written without
// String/std::vector: RAM is the binding constraint, so every buffer is a
// fixed, stack-local array sized just large enough for its job.
namespace Transfer {
  void begin();
  bool poll(); // call every loop(); returns true if it consumed a full session
}
