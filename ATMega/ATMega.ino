#include <SPI.h>
#include <SSD1306Ascii.h>
#include <SSD1306AsciiWire.h>
#include <AltSoftSerial.h>
#include <MFRC522.h>

// ======================================================================
//  PIN DEFINITIONS
// ======================================================================
#define CONFIRM_BUTTON_PIN 2
#define CANCEL_BUTTON_PIN 3
#define RST_PIN 5
#define SS_PIN 10
#define LED1 A0
#define LED2 A1

// ======================================================================
//  STATE MACHINE
// ======================================================================
enum State {
  WAIT_ESP_READY,
  WAIT_STUDENT,
  WAIT_STUDENT_REPLY,
  SCAN_BOOK,
  WAIT_BOOK_CONFIRM,
  WAIT_BOOK_STATUS_REPLY,
  ACTION_SELECT,
  WAIT_ACTION_REPLY,
  END_TRANSACTION,
  WAIT_FOR_SERIAL
};

// ======================================================================
//  OLED DISPLAY
// ======================================================================
SSD1306AsciiWire display;

// ======================================================================
//  RFID
// ======================================================================
MFRC522 mfrc522(SS_PIN, RST_PIN);

// ======================================================================
//  QR READER
// ======================================================================
AltSoftSerial QRscanner;

// ======================================================================
//  BUFFERS
// ======================================================================
String studentFields[5];
String QRfields[6];
String lastScannedQR = "";

// ======================================================================
//  BUTTON HANDLING
// ======================================================================
bool lastConfirm = false;
bool lastCancel = false;

bool confirmPressed() {
  bool state = !digitalRead(CONFIRM_BUTTON_PIN);
  bool pressed = (state && !lastConfirm);
  lastConfirm = state;
  return pressed;
}

bool cancelPressed() {
  bool state = !digitalRead(CANCEL_BUTTON_PIN);
  bool pressed = (state && !lastCancel);
  lastCancel = state;
  return pressed;
}

// ======================================================================
//  LED CONTROL
// ======================================================================
void ledBlue() {
  digitalWrite(LED1, HIGH);
  digitalWrite(LED2, HIGH);
}
void ledGreen() {
  digitalWrite(LED1, HIGH);
  digitalWrite(LED2, LOW);
}
void ledYellow() {
  digitalWrite(LED1, LOW);
  digitalWrite(LED2, HIGH);
}
void ledOff() {
  digitalWrite(LED1, LOW);
  digitalWrite(LED2, LOW);
}

// ======================================================================
//  DISPLAY
// ======================================================================
void showMessage(String msg) {
  display.clear();
  display.setCursor(0, 0);

  int maxCharsPerLine = 20;
  String lineBuffer = "";

  for (int i = 0; i < msg.length(); i++) {
    char c = msg[i];

    if (c == ' ' || c == '\n') {
      if (lineBuffer.length() >= maxCharsPerLine) {
        display.println(lineBuffer);
        lineBuffer = "";
      }
      if (c == '\n') {
        display.println(lineBuffer);
        lineBuffer = "";
      } else {
        lineBuffer += c;
      }
    } else {
      lineBuffer += c;
    }
  }

  if (lineBuffer.length() > 0) display.println(lineBuffer);
}

void showStudent() {
  String msg = "FN: " + studentFields[0] + "\nLN: " + studentFields[2] + "\nSN: " + studentFields[3] +"\n\nConfirm or Cancel";
  showMessage(msg);
}

void showBook() {
  String msg = "Title: " + QRfields[0] + "\nAuthor: " + QRfields[1] + "\nISBN: " + QRfields[2] + "\nBookID: " + QRfields[5] + "\nConfirm or Cancel?";
  showMessage(msg);
}

// ======================================================================
//  PARSE CSV
// ======================================================================
void parseCSV(String data, String outArr[]) {
  int index = 0;
  int start = 0;

  for (int i = 0; i < data.length() && index < 6; i++) {
    if (data[i] == ',') {
      outArr[index++] = data.substring(start, i);
      start = i + 1;
    }
  }
  if (index < 6)
    outArr[index] = data.substring(start);
}

String readQR() {
  static String buffer = "";
  static unsigned long lastCharTime = 0;

  while (QRscanner.available()) {
    char c = QRscanner.read();
    buffer += c;
    lastCharTime = millis();
  }

  // If data stopped coming for 80 ms, consider QR complete
  if (buffer.length() > 0 && millis() - lastCharTime > 80) {
    String result = buffer;
    buffer = "";
    result.trim();
    return result;
  }

  return "";
}

// ======================================================================
//  RFID
// ======================================================================
String readBlock(byte block, MFRC522::MIFARE_Key key) {
  byte buffer[18];
  byte size = sizeof(buffer);

  if (mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(mfrc522.uid)) != MFRC522::STATUS_OK)
    return "";

  if (mfrc522.MIFARE_Read(block, buffer, &size) != MFRC522::STATUS_OK)
    return "";

  String result = "";
  for (int i = 0; i < 16 && buffer[i] != 0; i++)
    result += (char)buffer[i];

  return result;
}

// ======================================================================
//  RFID READ SEQUENCE
// ======================================================================
bool scanRFID() {
  if (!mfrc522.PICC_IsNewCardPresent()) return false;
  if (!mfrc522.PICC_ReadCardSerial()) return false;

  MFRC522::MIFARE_Key key;
  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

  studentFields[0] = readBlock(4, key);
  studentFields[1] = readBlock(5, key);
  studentFields[2] = readBlock(6, key);
  studentFields[3] = readBlock(8, key);
  studentFields[4] = readBlock(9, key);

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();

  if (studentFields[0] != "") return true;
  else return false;
}

// ======================================================================
//  ESP MESSAGE
// ======================================================================
String checkESP() {
  if (!Serial.available()) return "";
  String s = Serial.readStringUntil('\n');
  s.trim();
  if (s.startsWith("esp:")) return s.substring(4);
  return "";
}

// ======================================================================
//  STATE VARIABLES
// ======================================================================
State state = WAIT_ESP_READY;
State returnState = WAIT_STUDENT;
String lastESP = "";

unsigned long espWaitStart = 0;
const unsigned long ESP_TIMEOUT = 90000;
unsigned long idleStart = 0;
const unsigned long IDLE_TIMEOUT = 20000;   // 20 seconds (adjust if needed)

void gotoSerialWait(State nextState) {
  returnState = nextState;
  espWaitStart = millis();
  state = WAIT_FOR_SERIAL;
}

void resetIdleTimer() {
  idleStart = millis();
}

// ======================================================================
//  SETUP
// ======================================================================
void setup() {
  Serial.begin(9600);
  QRscanner.begin(9600);

  SPI.begin();
  mfrc522.PCD_Init();

  pinMode(CONFIRM_BUTTON_PIN, INPUT_PULLUP);
  pinMode(CANCEL_BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);

  Wire.begin();
  display.begin(&SH1106_128x64, 0x3C);
  display.setFont(System5x7);

  ledOff();
  Serial.print(F("SystemStarted"));
  Serial.println(F(" "));
  showMessage("Waiting for ESP...");
}

// ======================================================================
//  MAIN LOOP
// ======================================================================
void loop() {

  String espTemp = checkESP();
  if (espTemp.length() > 0) {
    lastESP = espTemp;
    // For Troubleshooting ESP Connection 
    // Serial.print(F("lastESP: "));
    // Serial.println(lastESP);
  }

  switch (state) {

    case WAIT_FOR_SERIAL:
      state = returnState;
      break;

    case WAIT_ESP_READY:
      if (lastESP == "esp ready") {
        lastESP = "";
        showMessage(F("System Ready!\nTap ID to start."));
        resetIdleTimer();
        state = WAIT_STUDENT;
      }
      break;
      
    case WAIT_STUDENT:

      // Auto reset if user walks away
      if (millis() - idleStart > IDLE_TIMEOUT && studentFields[0] != "") {
        ledGreen();
        showMessage(F("Session Timeout"));
        delay(1000);
        state = END_TRANSACTION;
        break;
      }

      // Scan RFID only if we don't have a student yet
      if (studentFields[0] == "") {
        if (scanRFID()) {
          ledBlue();
          showStudent();
          resetIdleTimer();
        }
      }

      if (confirmPressed() && studentFields[0] != "") {
        ledBlue();

        Serial.println(F("[Arduino] Sending student to ESP..."));
        Serial.print("student:");
        Serial.print(studentFields[0]); Serial.print(",");
        Serial.print(studentFields[1]); Serial.print(",");
        Serial.print(studentFields[2]); Serial.print(",");
        Serial.print(studentFields[3]); Serial.print(",");
        Serial.println(studentFields[4]);

        showMessage(F("Verifying student..."));
        gotoSerialWait(WAIT_STUDENT_REPLY);
      }

      if (cancelPressed() && studentFields[0] != "") {
        resetIdleTimer();   
        state = END_TRANSACTION;
      }

    break;

    case WAIT_STUDENT_REPLY:

      if (millis() - espWaitStart > ESP_TIMEOUT) {
        ledYellow();
        lastESP = "";
        showMessage(F("ESP Timeout\n\nConfirm to retry\nCancel to end"));
        resetIdleTimer();
        state = WAIT_STUDENT;
        break;
      }

      if (lastESP == "Student Found") {
        lastESP = "";
        showMessage(F("Student Verified!\n\nScan a book\nOr cancel to end"));
        resetIdleTimer();
        state = SCAN_BOOK;
      } else if (lastESP == "New student document created successfully.") {
        lastESP = "";
        showMessage(F("Student Added!\n\nScan a book\nOr cancel to end"));
        resetIdleTimer();
        state = SCAN_BOOK;
      } else if (lastESP.startsWith("Error")) {
        ledYellow();
        lastESP = "";
        showMessage(F("Error\n\nConfirm to retry\nCancel to end"));
        resetIdleTimer();
        state = WAIT_STUDENT;
      } else if (lastESP.startsWith("WiFi")) {
        ledYellow();
        lastESP = "";
        showMessage(F("WiFi Disconnected\n\nConfirm to retry\nCancel to end"));
        resetIdleTimer();
        state = WAIT_STUDENT;
      }

      break;

    case SCAN_BOOK:

      // Auto reset if user walks away
      if (millis() - idleStart > IDLE_TIMEOUT) {
        ledGreen();
        showMessage(F("Session Timeout."));
        delay(1000);
        state = END_TRANSACTION;
        break;
      }

      if (lastScannedQR == "") {
        lastScannedQR = readQR();
        if (lastScannedQR.length() > 0) {
          ledBlue();
          parseCSV(lastScannedQR, QRfields);
          showBook();
          resetIdleTimer();

          // For Troubleshooting QR reading
          // Serial.print(F("[Arduino] QR detected: "));
          // Serial.println(lastScannedQR);

          state = WAIT_BOOK_CONFIRM;
        }
      }

      if (cancelPressed()) state = END_TRANSACTION;

      break;

    case WAIT_BOOK_CONFIRM:

      // Auto reset if user walks away
      if (millis() - idleStart > IDLE_TIMEOUT) {
        ledGreen();
        showMessage(F("Book Confirm Timeout.\n\nScan Again or Cancel"));
        delay(1000);
        resetIdleTimer();
        state = SCAN_BOOK;
        break;
      }

      if (confirmPressed()) {
        showMessage(F("Searching book..."));
        Serial.print("searchBook:");
        Serial.print(QRfields[2]); Serial.print(",");
        Serial.println(QRfields[5]);

        lastScannedQR = "";
        gotoSerialWait(WAIT_BOOK_STATUS_REPLY);
      }

      if (cancelPressed()) {
        showMessage(F("Book adding canceled.\nScan again or Cancel"));
        lastScannedQR = "";
        ledGreen();
        resetIdleTimer();
        state = SCAN_BOOK;
      }

      break;

    case WAIT_BOOK_STATUS_REPLY:

      if (millis() - espWaitStart > ESP_TIMEOUT) {
        ledYellow();
        lastESP = "";
        showMessage(F("ESP Timeout.\n\nRescan Book or\nCancel to end"));
        resetIdleTimer();
        state = SCAN_BOOK;
        break;
      }

      if (lastESP == "bookAvailability:1") {
        showMessage(F("Book Available!\n\nConfirm=Borrow\nCancel=Cancel"));
        state = ACTION_SELECT;
      } else if (lastESP == "bookAvailability:0") {
        showMessage(F("Book Borrowed!\n\nConfirm=Return\nCancel=Cancel"));
        state = ACTION_SELECT;
      } else if (lastESP == "No matching document found.") {
        showMessage(F("Book not found.\n\nConfirm=Add\nCancel=Cancel"));
        state = ACTION_SELECT;
      } else if (lastESP.startsWith("Error")) {
        ledYellow();
        lastESP = "";
        showMessage(F("Error\n\nScan again or\nCancel to end"));
        resetIdleTimer();
        state = SCAN_BOOK;
      } else if (lastESP.startsWith("WiFi")) {
        ledYellow();
        lastESP = "";
        showMessage(F("WiFi Disconnected\n\nScan again or\nCancel to end"));
        resetIdleTimer();
        state = SCAN_BOOK;
      }

      break;

     case ACTION_SELECT:

      if (confirmPressed()) {
        if (lastESP == "bookAvailability:1") {
          Serial.print("borrow:");
          Serial.print(QRfields[2]);
          Serial.print(",");
          Serial.print(QRfields[5]);
          Serial.print(",");
          Serial.println(studentFields[3]);
        } else if (lastESP == "bookAvailability:0") {
          Serial.print("return:");
          Serial.print(QRfields[2]);
          Serial.print(",");
          Serial.print(QRfields[5]);
          Serial.print(",");
          Serial.println(studentFields[3]);
        } else if (lastESP == "No matching document found.") {
          Serial.print("addBook:");
          Serial.print(QRfields[2]);
          Serial.print(",");
          Serial.print(QRfields[5]);
          Serial.print(",");
          Serial.print(QRfields[0]);
          Serial.print(",");
          Serial.print(QRfields[1]);
          Serial.print(",");
          Serial.print(QRfields[3]);
          Serial.print(",");
          Serial.println(QRfields[4]);
        } else if (lastESP.startsWith("Error")) {
          ledYellow();
          lastESP = "";
          showMessage(F("Error\n\nScan again or\nCancel to end"));
          resetIdleTimer();
          state = SCAN_BOOK;
        } else if (lastESP.startsWith("WiFi")) {
          ledYellow();
          lastESP = "";
          showMessage(F("WiFi Disconnected\n\nScan again or\nCancel to end"));
          resetIdleTimer();
          state = SCAN_BOOK;
        }

        showMessage(F("Processing action..."));
        lastESP = "";
        gotoSerialWait(WAIT_ACTION_REPLY);
      }

      if (cancelPressed()) {
        showMessage(F("Action canceled.\n\nScan next book or\nCancel to end"));
        ledGreen();
        resetIdleTimer();
        state = SCAN_BOOK;
      }

      break;

    case WAIT_ACTION_REPLY:

      if (millis() - espWaitStart > ESP_TIMEOUT) {
        ledYellow();
        lastESP = "";
        showMessage(F("ESP Timeout.\n\nScan to retry \nCancel=Abort"));
        resetIdleTimer();
        state = SCAN_BOOK;
        break;
      }
      
      if (lastESP.endsWith("_ok")) {
        lastESP = "";
        ledGreen();
        showMessage(F("Transaction complete!\n\nScan next or\nPress cancel to end"));
        resetIdleTimer();
        state = SCAN_BOOK;
      } else if (lastESP.startsWith("Error")) {
        ledYellow();
        lastESP = "";
        showMessage(F("Error\n\nScan Again or\nPress cancel to end"));
        resetIdleTimer();
        state = SCAN_BOOK;
      } else if (lastESP.startsWith("WiFi")) {
        ledYellow();
        lastESP = "";
        showMessage(F("Error\n\nScan Again or\nPress cancel to end"));
        resetIdleTimer();
        state = SCAN_BOOK;
      }
      
      break;

    case END_TRANSACTION:

      studentFields[0] = "";
      studentFields[1] = "";
      studentFields[2] = "";
      studentFields[3] = "";
      studentFields[4] = "";
      
      showMessage(F("Transaction Ended"));
      delay(500);
      ledOff();
      showMessage(F("Tap Student ID"));
      resetIdleTimer();
      state = WAIT_STUDENT;

      break;
  }
}