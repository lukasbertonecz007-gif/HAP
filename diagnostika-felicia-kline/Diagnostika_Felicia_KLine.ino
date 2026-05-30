/*
  Diagnostika_Felicia_KLine.ino

  Testovaci Arduino sketch pro Skoda Felicia 1.3 MPI / SIMOS 2P
  pres K-line diagnostiku VAG KW1281.

  Overeny hardware:
  - Arduino Nano / Uno 5 V
  - K-line prevodnik z BC547 tranzistoru
  - AltSoftSerial: RX = D8, TX = D9
  - Serial Monitor: 115200 baud

  Jak K-line funguje:
  - Klidovy stav linky je HIGH.
  - Logicka nula se dela stazenim K-line na GND.
  - Je to jeden vodic, half-duplex komunikace.
  - Pri vysilani muze RX videt echo vlastnich TX bajtu.

  Bezpecnost:
  - Sketch posila jen cteci diagnosticke dotazy.
  - Neobsahuje mazani chyb, adaptace, kodovani ani zapis do ECU.
  - Nic neposila v rychle nekonecne smycce.
*/

#include <Arduino.h>
#include <AltSoftSerial.h>

// ---------------------------------------------------------------------------
// Nastaveni
// ---------------------------------------------------------------------------

const uint8_t KLINE_RX_PIN = 8;  // pevny RX pin knihovny AltSoftSerial na Uno/Nano
const uint8_t KLINE_TX_PIN = 9;  // pevny TX pin knihovny AltSoftSerial na Uno/Nano

const unsigned long DEBUG_BAUD = 115200;
const unsigned long KLINE_BAUD = 9600;

// Felicia/Simos 2P odpovida na starsi VAG adresu motorove jednotky 0x01.
const uint8_t VAG_ENGINE_ADDR = 0x01;

// Zapnout pro tranzistorovy prevodnik, kde RX vidi vlastni TX.
#define HANDLE_ECHO 1

const uint16_t SLOW_BIT_MS = 200;  // 5 baud = 200 ms na bit
const uint16_t SLOW_IDLE_BEFORE_MS = 300;
const uint16_t SLOW_IDLE_AFTER_MS = 0;
const uint16_t AFTER_SERIAL_SWITCH_MS = 5;

const uint16_t ECU_FIRST_BYTE_TIMEOUT_MS = 3000;
const uint16_t ECU_INTER_BYTE_TIMEOUT_MS = 120;
const uint16_t ECHO_BYTE_TIMEOUT_MS = 35;

const uint16_t KW1281_RESPONSE_TIMEOUT_MS = 2000;
const uint16_t KW1281_BYTE_TIMEOUT_MS = 120;
const uint16_t KW1281_COMPLEMENT_TIMEOUT_MS = 80;
const uint16_t KW1281_COMPLEMENT_DELAY_MS = 2;
const uint16_t KW1281_INIT_COMPLEMENT_DELAY_MS = 40;
const uint16_t KW1281_BYTE_GAP_AFTER_COMPLEMENT_MS = 8;
const uint16_t KW1281_BLOCK_DELAY_MS = 12;

const uint8_t MAX_FRAME_LEN = 96;
const uint8_t KW1281_MAX_DATA_LEN = 64;
const uint8_t PENDING_MAX = 16;

const uint8_t KWP1281_ACKNOWLEDGE = 0x09;
const uint8_t KWP1281_REFUSE = 0x0A;
const uint8_t KWP1281_REQUEST_FAULT_CODES = 0x07;
const uint8_t KWP1281_REQUEST_GROUP_READING = 0x29;
const uint8_t KWP1281_RECEIVE_ID_DATA = 0xF6;
const uint8_t KWP1281_RECEIVE_FAULT_CODES = 0xFC;
const uint8_t KWP1281_RECEIVE_GROUP_HEADER = 0x02;
const uint8_t KWP1281_RECEIVE_GROUP_READING = 0xE7;
const uint8_t KWP1281_RECEIVE_BASIC_SETTING = 0xF4;

AltSoftSerial klineSerial;

uint8_t kw1281LastSequence = 0;
uint8_t pendingBytes[PENDING_MAX];
uint8_t pendingLen = 0;
uint8_t pendingPos = 0;

// ---------------------------------------------------------------------------
// Pomocne funkce
// ---------------------------------------------------------------------------

void printHexByte(uint8_t b) {
  if (b < 0x10) Serial.print(F("0"));
  Serial.print(b, HEX);
}

void printFrameLine(const char *label, const uint8_t *buf, uint8_t len) {
  Serial.print(label);
  Serial.print(F(" len="));
  Serial.print(len);
  Serial.print(F(" :"));
  for (uint8_t i = 0; i < len; i++) {
    Serial.print(F(" "));
    printHexByte(buf[i]);
  }
  Serial.println();
}

void resetPending() {
  pendingLen = 0;
  pendingPos = 0;
}

void pushPending(uint8_t b) {
  if (pendingLen < PENDING_MAX) {
    pendingBytes[pendingLen++] = b;
  }
}

int popPending() {
  if (pendingPos < pendingLen) {
    uint8_t b = pendingBytes[pendingPos++];
    if (pendingPos >= pendingLen) resetPending();
    return b;
  }
  return -1;
}

int readRawByteWithTimeout(uint16_t timeoutMs) {
  unsigned long started = millis();
  while ((uint16_t)(millis() - started) < timeoutMs) {
    if (klineSerial.available() > 0) {
      return klineSerial.read();
    }
  }
  return -1;
}

int readByteWithTimeout(uint16_t timeoutMs) {
  int pending = popPending();
  if (pending >= 0) return pending;
  return readRawByteWithTimeout(timeoutMs);
}

uint8_t readFrameWithTimeout(uint8_t *buf, uint8_t maxLen,
                             uint16_t firstTimeoutMs,
                             uint16_t interByteTimeoutMs) {
  uint8_t len = 0;
  int b = readByteWithTimeout(firstTimeoutMs);
  if (b < 0) return 0;

  while (b >= 0 && len < maxLen) {
    buf[len++] = (uint8_t)b;
    Serial.print(F("RX: 0x"));
    printHexByte((uint8_t)b);
    Serial.print(F("  DEC: "));
    Serial.println(b, DEC);
    b = readByteWithTimeout(interByteTimeoutMs);
  }

  printFrameLine("RAW", buf, len);
  return len;
}

void clearEcho(const uint8_t *sent, uint8_t len) {
#if HANDLE_ECHO
  uint8_t discarded = 0;
  while (discarded < len) {
    int b = readRawByteWithTimeout(ECHO_BYTE_TIMEOUT_MS);
    if (b < 0) break;

    if ((uint8_t)b != sent[discarded]) {
      pushPending((uint8_t)b);
      break;
    }
    discarded++;
  }
#else
  (void)sent;
  (void)len;
#endif
}

void stopKLineSerialForBitBang() {
  klineSerial.end();
  delay(20);
  resetPending();
  pinMode(KLINE_RX_PIN, INPUT);
  pinMode(KLINE_TX_PIN, OUTPUT);
  digitalWrite(KLINE_TX_PIN, HIGH);
}

void startKLineSerial() {
  resetPending();
  klineSerial.begin(KLINE_BAUD);
  delay(AFTER_SERIAL_SWITCH_MS);
}

void sendKLineByteRaw(uint8_t b) {
  klineSerial.write(b);
  klineSerial.flush();
}

void send5BaudByte(uint8_t b) {
  Serial.print(F("sending 5 baud init byte 0x"));
  printHexByte(b);
  Serial.println();

  digitalWrite(KLINE_TX_PIN, HIGH);
  delay(SLOW_IDLE_BEFORE_MS);

  digitalWrite(KLINE_TX_PIN, LOW);  // start bit
  delay(SLOW_BIT_MS);

  for (uint8_t bit = 0; bit < 8; bit++) {
    bool one = (b & (1 << bit)) != 0;
    digitalWrite(KLINE_TX_PIN, one ? HIGH : LOW);
    delay(SLOW_BIT_MS);
  }

  digitalWrite(KLINE_TX_PIN, HIGH);  // stop bit + idle
  delay(SLOW_BIT_MS);
  delay(SLOW_IDLE_AFTER_MS);
}

void kw1281SendComplement(uint8_t b) {
  uint8_t complement = b ^ 0xFF;
  sendKLineByteRaw(complement);
  delay(2);
  clearEcho(&complement, 1);
}

bool kw1281ReadByte(uint8_t *out, uint16_t timeoutMs, bool sendComplement) {
  int b = readByteWithTimeout(timeoutMs);
  if (b < 0) {
    Serial.print(F("KW1281 timeout after "));
    Serial.print(timeoutMs);
    Serial.println(F(" ms"));
    return false;
  }

  *out = (uint8_t)b;
  if (sendComplement) {
    delay(KW1281_COMPLEMENT_DELAY_MS);
    kw1281SendComplement(*out);
  }
  return true;
}

bool kw1281SendByteAndWaitComplement(uint8_t b, bool waitComplement) {
  sendKLineByteRaw(b);
  delay(2);
  clearEcho(&b, 1);

  if (!waitComplement) return true;

  int c = readByteWithTimeout(KW1281_COMPLEMENT_TIMEOUT_MS);
  if (c < 0) {
    Serial.println(F("KW1281 timeout waiting for ECU complement"));
    return false;
  }

  uint8_t expected = b ^ 0xFF;
  if ((uint8_t)c != expected) {
    Serial.print(F("KW1281 complement mismatch. Expected 0x"));
    printHexByte(expected);
    Serial.print(F(", got 0x"));
    printHexByte((uint8_t)c);
    Serial.println();
    return false;
  }

  delay(KW1281_BYTE_GAP_AFTER_COMPLEMENT_MS);
  return true;
}

bool kw1281SendBlock(uint8_t messageType, const uint8_t *data, uint8_t dataLen) {
  delay(KW1281_BLOCK_DELAY_MS);

  uint8_t length = 3 + dataLen;  // seq + type + data + final 0x03
  uint8_t sequence = kw1281LastSequence + 1;

  if (!kw1281SendByteAndWaitComplement(length, true)) return false;
  if (!kw1281SendByteAndWaitComplement(sequence, true)) return false;
  if (!kw1281SendByteAndWaitComplement(messageType, true)) return false;

  for (uint8_t i = 0; i < dataLen; i++) {
    if (!kw1281SendByteAndWaitComplement(data[i], true)) return false;
  }

  if (!kw1281SendByteAndWaitComplement(0x03, false)) return false;
  kw1281LastSequence = sequence;
  return true;
}

bool kw1281ReceiveBlock(uint8_t *messageType, uint8_t *data, uint8_t *dataLen,
                        uint16_t firstByteTimeoutMs, bool mayRepeatKeywords) {
  uint8_t length = 0;

  while (true) {
    if (!kw1281ReadByte(&length, firstByteTimeoutMs, false)) return false;

    if (length == 0x55 && mayRepeatKeywords) {
      uint8_t kw1 = 0;
      uint8_t kw2 = 0;
      if (!kw1281ReadByte(&kw1, KW1281_BYTE_TIMEOUT_MS, false)) return false;
      if (!kw1281ReadByte(&kw2, KW1281_BYTE_TIMEOUT_MS, false)) return false;
      delay(KW1281_INIT_COMPLEMENT_DELAY_MS);
      kw1281SendComplement(kw2);
      continue;
    }
    break;
  }

  if (length < 3 || length > (KW1281_MAX_DATA_LEN + 3)) {
    Serial.print(F("KW1281 invalid block length: "));
    Serial.println(length);
    return false;
  }

  delay(KW1281_COMPLEMENT_DELAY_MS);
  kw1281SendComplement(length);

  uint8_t seq = 0;
  if (!kw1281ReadByte(&seq, KW1281_BYTE_TIMEOUT_MS, true)) return false;
  kw1281LastSequence = seq;

  if (!kw1281ReadByte(messageType, KW1281_BYTE_TIMEOUT_MS, true)) return false;

  *dataLen = length - 3;
  for (uint8_t i = 0; i < *dataLen; i++) {
    if (!kw1281ReadByte(&data[i], KW1281_BYTE_TIMEOUT_MS, true)) return false;
  }

  uint8_t endByte = 0;
  if (!kw1281ReadByte(&endByte, KW1281_BYTE_TIMEOUT_MS, false)) return false;
  if (endByte != 0x03) {
    Serial.print(F("KW1281 unexpected end byte: 0x"));
    printHexByte(endByte);
    Serial.println();
    return false;
  }

  return true;
}

// ---------------------------------------------------------------------------
// Inicializace KW1281
// ---------------------------------------------------------------------------

bool klineWakeSlowInit() {
  Serial.println();
  Serial.println(F("starting KW1281 slow init"));
  Serial.println(F("K-line idle HIGH"));

  kw1281LastSequence = 0;
  stopKLineSerialForBitBang();
  send5BaudByte(VAG_ENGINE_ADDR);

  Serial.println(F("switching to 9600 baud"));
  startKLineSerial();

  Serial.println(F("waiting for ECU response"));
  uint8_t frame[MAX_FRAME_LEN];
  uint8_t len = readFrameWithTimeout(frame, MAX_FRAME_LEN,
                                     ECU_FIRST_BYTE_TIMEOUT_MS,
                                     ECU_INTER_BYTE_TIMEOUT_MS);
  if (len == 0) {
    Serial.println(F("init failed: no ECU response"));
    return false;
  }

  int syncIndex = -1;
  for (uint8_t i = 0; i < len; i++) {
    if (frame[i] == 0x55) {
      syncIndex = i;
      break;
    }
  }

  if (syncIndex < 0 || (syncIndex + 2) >= len) {
    Serial.println(F("init failed: sync/keywords not complete"));
    return false;
  }

  uint8_t key1 = frame[syncIndex + 1];
  uint8_t key2 = frame[syncIndex + 2];

  Serial.println(F("sync byte 0x55 is OK"));
  Serial.print(F("keyword 1: 0x"));
  printHexByte(key1);
  Serial.print(F("  keyword 2: 0x"));
  printHexByte(key2);
  Serial.println();

  uint8_t ack = key2 ^ 0xFF;
  delay(KW1281_INIT_COMPLEMENT_DELAY_MS);
  sendKLineByteRaw(ack);
  delay(2);
  clearEcho(&ack, 1);

  Serial.println(F("KW1281 init OK"));
  return true;
}

bool kw1281ConsumeIdentification() {
  Serial.println(F("reading ECU identification"));

  uint8_t type = 0;
  uint8_t data[KW1281_MAX_DATA_LEN];
  uint8_t len = 0;

  if (!kw1281ReceiveBlock(&type, data, &len, KW1281_RESPONSE_TIMEOUT_MS, true)) {
    Serial.println(F("identification read failed"));
    return false;
  }

  for (uint8_t block = 0; block < 5; block++) {
    if (type == KWP1281_ACKNOWLEDGE) {
      Serial.println(F("identification done"));
      return true;
    }

    if (type != KWP1281_RECEIVE_ID_DATA) {
      Serial.print(F("unexpected identification block type: 0x"));
      printHexByte(type);
      Serial.println();
      return false;
    }

    Serial.print(F("ID: "));
    for (uint8_t i = 0; i < len; i++) {
      char ch = (char)data[i];
      Serial.print((ch >= 32 && ch <= 126) ? ch : '.');
    }
    Serial.println();

    if (!kw1281SendBlock(KWP1281_ACKNOWLEDGE, NULL, 0)) {
      Serial.println(F("failed to ACK identification block"));
      return false;
    }

    if (!kw1281ReceiveBlock(&type, data, &len, KW1281_RESPONSE_TIMEOUT_MS, false)) {
      Serial.println(F("identification timeout"));
      return false;
    }
  }

  return true;
}

// ---------------------------------------------------------------------------
// Cteni merenych hodnot
// ---------------------------------------------------------------------------

bool kw1281RequestGroup(uint8_t group, uint8_t *data, uint8_t *len, uint8_t *type) {
  uint8_t param[] = {group};

  if (!kw1281SendBlock(KWP1281_REQUEST_GROUP_READING, param, 1)) {
    Serial.print(F("failed to request group "));
    Serial.println(group);
    return false;
  }

  if (!kw1281ReceiveBlock(type, data, len, KW1281_RESPONSE_TIMEOUT_MS, false)) {
    Serial.print(F("group timeout: "));
    Serial.println(group);
    return false;
  }

  if (*type == KWP1281_REFUSE || *type == KWP1281_ACKNOWLEDGE) {
    Serial.print(F("group refused/empty: "));
    Serial.println(group);
    return false;
  }

  return true;
}

bool kw1281ReadRawGroupBody(uint8_t group, uint8_t *body, uint8_t *bodyLen) {
  uint8_t type = 0;
  uint8_t len = 0;
  uint8_t data[KW1281_MAX_DATA_LEN];

  if (!kw1281RequestGroup(group, data, &len, &type)) return false;

  if (type == KWP1281_RECEIVE_GROUP_HEADER) {
    delay(40);
    if (!kw1281RequestGroup(group, data, &len, &type)) return false;
  }

  if (type != KWP1281_RECEIVE_BASIC_SETTING && type != KWP1281_RECEIVE_GROUP_READING) {
    Serial.print(F("unexpected group response type: 0x"));
    printHexByte(type);
    Serial.println();
    return false;
  }

  memcpy(body, data, len);
  *bodyLen = len;
  return true;
}

void printLiveLine(uint16_t sample, double rpm, double voltage, double throttleAngle) {
  Serial.print(F("LIVE "));
  Serial.print(sample);
  Serial.print(F("  RPM="));
  Serial.print(rpm, 0);
  Serial.print(F(" rpm  Voltage="));
  Serial.print(voltage, 1);
  Serial.print(F(" V  Throttle="));
  Serial.print(throttleAngle, 1);
  Serial.println(F(" deg"));
}

void readLiveRpmVoltageThrottle() {
  Serial.println();
  Serial.println(F("Live data: RPM, battery voltage, throttle angle"));
  Serial.println(F("Read-only, 20 samples."));

  if (!klineWakeSlowInit()) return;
  if (!kw1281ConsumeIdentification()) return;

  uint8_t group3[KW1281_MAX_DATA_LEN];
  uint8_t group5[KW1281_MAX_DATA_LEN];
  uint8_t group3Len = 0;
  uint8_t group5Len = 0;

  for (uint16_t sample = 1; sample <= 20; sample++) {
    bool g3ok = kw1281ReadRawGroupBody(3, group3, &group3Len);
    delay(60);
    bool g5ok = kw1281ReadRawGroupBody(5, group5, &group5Len);

    if (!g3ok || group3Len < 3) {
      Serial.println(F("LIVE error: could not read group 003"));
      break;
    }
    if (!g5ok || group5Len < 2) {
      Serial.println(F("LIVE error: could not read group 005"));
      break;
    }

    // Overeno na SIMOS 2P:
    // group 003 byte 1 = otacky, raw * 32 rpm
    // group 003 byte 3 = uhel skrtici klapky, raw * 0.5 deg
    // group 005 byte 2 = napeti baterie, raw / 10 V
    double rpm = group3[0] * 32.0;
    double throttleAngle = group3[2] * 0.5;
    double voltage = group5[1] * 0.1;

    printLiveLine(sample, rpm, voltage, throttleAngle);
    delay(850);
  }

  Serial.println(F("live data finished"));
}

// ---------------------------------------------------------------------------
// Cteni chyb
// ---------------------------------------------------------------------------

void printFaultCode(uint8_t index, const uint8_t *triple) {
  uint16_t code = ((uint16_t)triple[0] << 8) | triple[1];
  uint8_t status = triple[2];

  Serial.print(F("DTC #"));
  Serial.print(index + 1);
  Serial.print(F(": raw="));
  printHexByte(triple[0]);
  Serial.print(F(" "));
  printHexByte(triple[1]);
  Serial.print(F(" "));
  printHexByte(triple[2]);
  Serial.print(F("  VAG/decimal="));
  if (code < 10000) Serial.print(F("0"));
  if (code < 1000) Serial.print(F("0"));
  if (code < 100) Serial.print(F("0"));
  if (code < 10) Serial.print(F("0"));
  Serial.print(code);
  Serial.print(F("  status=0x"));
  printHexByte(status);
  Serial.println();
}

void readFaultCodes() {
  Serial.println();
  Serial.println(F("Reading ECU fault codes, read-only"));

  if (!klineWakeSlowInit()) return;
  if (!kw1281ConsumeIdentification()) return;

  if (!kw1281SendBlock(KWP1281_REQUEST_FAULT_CODES, NULL, 0)) {
    Serial.println(F("failed to request fault codes"));
    return;
  }

  uint8_t totalFaults = 0;
  uint8_t type = 0;
  uint8_t data[KW1281_MAX_DATA_LEN];
  uint8_t len = 0;

  while (true) {
    if (!kw1281ReceiveBlock(&type, data, &len, KW1281_RESPONSE_TIMEOUT_MS, false)) {
      Serial.println(F("fault-code response timeout"));
      return;
    }

    if (type == KWP1281_REFUSE) {
      Serial.println(F("fault-code request refused"));
      return;
    }

    if (type == KWP1281_ACKNOWLEDGE) {
      Serial.print(F("fault-code read complete. Count: "));
      Serial.println(totalFaults);
      return;
    }

    if (type != KWP1281_RECEIVE_FAULT_CODES) {
      Serial.print(F("unexpected fault response type: 0x"));
      printHexByte(type);
      Serial.println();
      return;
    }

    if ((len % 3) != 0) {
      Serial.println(F("fault payload length is not multiple of 3"));
      return;
    }

    if (len == 3 && data[0] == 0xFF && data[1] == 0xFF && data[2] == 0x88) {
      Serial.println(F("ECU reports no fault codes: FF FF 88"));
      return;
    }

    for (uint8_t i = 0; i < len / 3; i++) {
      printFaultCode(totalFaults, &data[i * 3]);
      totalFaults++;
    }

    if (!kw1281SendBlock(KWP1281_ACKNOWLEDGE, NULL, 0)) {
      Serial.println(F("failed to request next fault block"));
      return;
    }
  }
}

// ---------------------------------------------------------------------------
// Hardware test a menu
// ---------------------------------------------------------------------------

void hardwareRoundTripTest() {
  Serial.println();
  Serial.println(F("K-line hardware round-trip test"));
  Serial.println(F("No ECU command is sent."));

  stopKLineSerialForBitBang();

  digitalWrite(KLINE_TX_PIN, HIGH);
  delay(150);
  int idleHigh = digitalRead(KLINE_RX_PIN);

  digitalWrite(KLINE_TX_PIN, LOW);
  delay(150);
  int drivenLow = digitalRead(KLINE_RX_PIN);

  digitalWrite(KLINE_TX_PIN, HIGH);
  delay(150);
  int releasedHigh = digitalRead(KLINE_RX_PIN);

  Serial.print(F("TX HIGH -> RX "));
  Serial.println(idleHigh == HIGH ? F("HIGH") : F("LOW"));
  Serial.print(F("TX LOW  -> RX "));
  Serial.println(drivenLow == HIGH ? F("HIGH") : F("LOW"));
  Serial.print(F("TX HIGH -> RX "));
  Serial.println(releasedHigh == HIGH ? F("HIGH") : F("LOW"));

  if (idleHigh == HIGH && drivenLow == LOW && releasedHigh == HIGH) {
    Serial.println(F("hardware looks OK"));
  } else {
    Serial.println(F("hardware test failed"));
  }
}

void printMenu() {
  Serial.println();
  Serial.println(F("=============================================="));
  Serial.println(F("Diagnostika Felicia K-line / KW1281"));
  Serial.println(F("Serial Monitor: 115200 baud"));
  Serial.println(F("Pins: D8=RX, D9=TX"));
  Serial.println(F("Commands:"));
  Serial.println(F("  l = live RPM, voltage, throttle angle"));
  Serial.println(F("  f = read ECU fault codes"));
  Serial.println(F("  t = hardware round-trip test"));
  Serial.println(F("  ? = menu"));
  Serial.println(F("Read-only commands only."));
  Serial.println(F("=============================================="));
}

void handleCommand(char c) {
  if (c == '\r' || c == '\n' || c == ' ') return;

  if (c == 'l' || c == 'L') {
    readLiveRpmVoltageThrottle();
  } else if (c == 'f' || c == 'F') {
    readFaultCodes();
  } else if (c == 't' || c == 'T') {
    hardwareRoundTripTest();
  } else if (c == '?') {
    printMenu();
  } else {
    Serial.print(F("Unknown command: "));
    Serial.println(c);
    printMenu();
  }
}

void setup() {
  Serial.begin(DEBUG_BAUD);
  while (!Serial) {
    ;
  }

  stopKLineSerialForBitBang();

  Serial.println();
  Serial.println(F("Boot OK"));
  printMenu();
}

void loop() {
  while (Serial.available() > 0) {
    handleCommand((char)Serial.read());
  }
}
