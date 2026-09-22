#include "Transfer.h"
#include "Config.h"
#include <Arduino.h>
#include <SD.h>
#include <vector>

namespace {
  String readLine() {
    String line = Serial.readStringUntil('\n');
    line.trim();
    return line;
  }

  void handlePut(const String &args) {
    int sp = args.indexOf(' ');
    if (sp < 0) {
      Serial.println("ERR bad PUT args");
      return;
    }
    String filename = args.substring(0, sp);
    long size = args.substring(sp + 1).toInt();
    if (!filename.startsWith("/")) filename = "/" + filename;

    if (SD.exists(filename)) SD.remove(filename);
    File f = SD.open(filename, FILE_WRITE);
    if (!f) {
      Serial.println("ERR could not open file for write");
      return;
    }
    Serial.println("OK");

    static uint8_t buf[TRANSFER_CHUNK_SIZE];
    long received = 0;
    bool ok = true;
    while (received < size) {
      int wanted = (int)min((long)TRANSFER_CHUNK_SIZE, size - received);
      int n = Serial.readBytes(buf, wanted);
      if (n != wanted) {
        ok = false;
        break;
      }
      f.write(buf, n);
      received += n;
      Serial.print("ACK ");
      Serial.println(received);
    }
    f.close();

    if (ok) {
      Serial.print("DONE ");
      Serial.println(received);
    } else {
      Serial.println("ERR incomplete transfer");
    }
  }

  void handleDelete(const String &args) {
    String filename = args;
    if (!filename.startsWith("/")) filename = "/" + filename;
    if (!SD.exists(filename)) {
      Serial.println("ERR no such file");
      return;
    }
    if (!SD.remove(filename)) {
      Serial.println("ERR could not delete");
      return;
    }
    Serial.println("OK");
  }

  void handleList() {
    File root = SD.open("/");
    if (!root) {
      Serial.println("ERR could not open SD root");
      return;
    }

    std::vector<String> lines;
    File entry = root.openNextFile();
    while (entry) {
      if (!entry.isDirectory()) {
        String name = entry.name();
        lines.push_back(name + " " + String((long)entry.size()));
      }
      entry.close();
      entry = root.openNextFile();
    }
    root.close();

    Serial.print("COUNT ");
    Serial.println((int)lines.size());
    for (auto &l : lines) Serial.println(l);
    Serial.println("ENDLIST");
  }

  void runSession() {
    Serial.println("READERINO v1");
    while (true) {
      String cmd = readLine();
      if (cmd.length() == 0) continue; // timed-out read with no data; keep waiting
      if (cmd == "BYE") {
        Serial.println("BYE");
        return;
      } else if (cmd.startsWith("PUT ")) {
        handlePut(cmd.substring(4));
      } else if (cmd == "LIST") {
        handleList();
      } else if (cmd.startsWith("DELETE ")) {
        handleDelete(cmd.substring(7));
      } else {
        Serial.println("ERR unknown command");
      }
    }
  }
}

void Transfer::begin() {
  Serial.begin(TRANSFER_BAUD);
  Serial.setTimeout(TRANSFER_TIMEOUT_MS);
}

bool Transfer::poll() {
  if (!Serial.available()) return false;
  String line = readLine();
  if (line != "HELLO") return false;
  runSession();
  return true;
}
