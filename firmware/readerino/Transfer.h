#pragma once

// A tiny serial protocol that lets a host script push .txt files onto the SD
// card over the same USB connection used to program the board, since the SD
// reader is wired to the ESP32 rather than exposed to the host directly.
//
// Host <-> device text protocol (one line per message, newline-terminated):
//   host:   HELLO
//   device: READERINO v1
//
//   host:   PUT <filename> <size>
//   device: OK                          (or ERR <reason>)
//   host:   <size> raw bytes, sent in chunks
//   device: ACK <bytesReceivedSoFar>     (once per chunk)
//   device: DONE <totalBytesReceived>    (or ERR <reason>)
//
//   host:   LIST
//   device: COUNT <n>
//   device: <name> <size>                (n times)
//   device: ENDLIST
//
//   host:   DELETE <filename>
//   device: OK                          (or ERR <reason>)
//
//   host:   GET <filename>
//   device: SIZE <n>                    (or ERR <reason>)
//   device: <n> raw bytes
//
//   host:   SETPOS <catalogIndex> <line>
//   device: OK
//
//   host:   ADDBM <catalogIndex> <line>
//   device: OK
//
//   host:   BYE
//   device: BYE
namespace Transfer {
  void begin();
  // Call once per loop(). Returns true if it consumed a full transfer
  // session (i.e. saw "HELLO"); normal app logic should be skipped that tick.
  bool poll();
}
