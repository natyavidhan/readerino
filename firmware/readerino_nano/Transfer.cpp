#include "Transfer.h"
#include "Config.h"
#include "Storage.h"
#include <Arduino.h>
#include <SD.h>
#include <string.h>
#include <stdlib.h>

namespace {
  char lineBuf[40]; // command lines are short: "PUT /bxxxx.rbk 12345" etc.

  int readLine(char *buf, int maxLen) {
    int len = Serial.readBytesUntil('\n', buf, maxLen - 1);
    if (len > 0 && buf[len - 1] == '\r') len--;
    buf[len] = 0;
    return len;
  }

  void toPath(const char *arg, char *out) {
    if (arg[0] != '/') {
      out[0] = '/';
      strncpy(out + 1, arg, FILENAME_LEN - 2);
      out[FILENAME_LEN - 1] = 0;
    } else {
      strncpy(out, arg, FILENAME_LEN - 1);
      out[FILENAME_LEN - 1] = 0;
    }
  }

  void handlePut(char *args) {
    char *sp = strchr(args, ' ');
    if (!sp) {
      Serial.println(F("ERR bad PUT args"));
      return;
    }
    *sp = 0;
    long size = atol(sp + 1);

    char path[FILENAME_LEN];
    toPath(args, path);

    if (SD.exists(path)) SD.remove(path);
    File f = SD.open(path, O_WRITE | O_CREAT);
    if (!f) {
      Serial.println(F("ERR could not open file for write"));
      return;
    }
    Serial.println(F("OK"));

    uint8_t buf[TRANSFER_CHUNK_SIZE];
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
      Serial.print(F("ACK "));
      Serial.println(received);
    }
    f.close();

    if (ok) {
      Serial.print(F("DONE "));
      Serial.println(received);
    } else {
      Serial.println(F("ERR incomplete transfer"));
    }
  }

  void handleDelete(char *args) {
    char path[FILENAME_LEN];
    toPath(args, path);
    if (!SD.exists(path)) {
      Serial.println(F("ERR no such file"));
      return;
    }
    if (!SD.remove(path)) {
      Serial.println(F("ERR could not delete"));
      return;
    }
    Serial.println(F("OK"));
  }

  // "GET path" -> "SIZE n", then exactly n raw bytes of the file.
  void handleGet(char *args) {
    char path[FILENAME_LEN];
    toPath(args, path);
    File f = SD.open(path, O_READ);
    if (!f) {
      Serial.println(F("ERR no such file"));
      return;
    }
    Serial.print(F("SIZE "));
    Serial.println((long)f.size());
    uint8_t buf[TRANSFER_CHUNK_SIZE];
    int n;
    while ((n = f.read(buf, sizeof(buf))) > 0) Serial.write(buf, n);
    f.close();
  }


  void runSession() {
    Serial.println(F("READERINO-NANO v1"));
    while (true) {
      int len = readLine(lineBuf, sizeof(lineBuf));
      if (len == 0) continue; // timed-out read with no data; keep waiting
      if (strcmp(lineBuf, "BYE") == 0) {
        Serial.println(F("BYE"));
        return;
      } else if (strncmp(lineBuf, "PUT ", 4) == 0) {
        handlePut(lineBuf + 4);
      } else if (strncmp(lineBuf, "GET ", 4) == 0) {
        handleGet(lineBuf + 4);
      } else if (strncmp(lineBuf, "DELETE ", 7) == 0) {
        handleDelete(lineBuf + 7);
      } else {
        Serial.println(F("ERR unknown command"));
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
  int len = readLine(lineBuf, sizeof(lineBuf));
  if (len == 0 || strcmp(lineBuf, "HELLO") != 0) return false;
  runSession();
  return true;
}
