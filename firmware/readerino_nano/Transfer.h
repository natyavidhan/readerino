#pragma once

// Serial file transfer protocol used by packer/push_to_sd.py: HELLO, then
// PUT/GET/DELETE, then BYE. (There's no LIST: the SD library's directory
// walker costs ~600 bytes of flash this build doesn't have; GET the
// catalog instead.) Kept minimal for flash, and written without
// String/std::vector: RAM is the binding constraint, so every buffer is a
// fixed, stack-local array sized just large enough for its job.
namespace Transfer {
  void begin();
  bool poll(); // call every loop(); returns true if it consumed a full session
}
