// UNO full with RC522 + R307 (Adafruit_Fingerprint) + SoftwareSerial to ESP32
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>
#include <SoftwareSerial.h>
#include <Adafruit_Fingerprint.h>

#define RST_PIN 9
#define SS_PIN 10

MFRC522 rfid(SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(0x27,16,2);

// Chân giao tiếp với ESP32 (SoftwareSerial)
#define ESP_RX_PIN 6  // UNO RX pin (nối TX của ESP32)
#define ESP_TX_PIN 7  // UNO TX pin (nối RX của ESP32)
SoftwareSerial espSerial(ESP_RX_PIN, ESP_TX_PIN); // RX, TX (ESP32 <-> UNO)

// Chân nối R307 fingerprint (SoftwareSerial)
#define FP_RX_PIN 2   // UNO RX (nối TX của R307)
#define FP_TX_PIN 3   // UNO TX (nối RX của R307)
SoftwareSerial fpSerial(FP_RX_PIN, FP_TX_PIN); // RX, TX cho R307
Adafruit_Fingerprint finger(&fpSerial); 

String uidToSend = "";
String response = "";

// Admin UID
const String ADMIN_UID = "54:4D:C9:05"; // Thẻ Admin

// R307 template id max (đặt an toàn; điều chỉnh nếu cần)
const int MAX_TEMPLATE_ID = 1000;

// helper để chuyển byte array UID thành chuỗi
String uidBytesToString(MFRC522::Uid &uid){
  String s = "";
  for(byte i=0;i<uid.size;i++){
    if(i!=0) s += ":";
    if(uid.uidByte[i] < 0x10) s += "0";
    s += String(uid.uidByte[i], HEX);
  }
  s.toUpperCase();
  return s;
}

// --- Fingerprint helper functions with timeouts ---
#define FP_STAGE_TIMEOUT 8000UL  // ms timeout cho mỗi stage

uint8_t getFingerprintEnroll(int id) {
  if(id <= 0 || id >= MAX_TEMPLATE_ID) return FINGERPRINT_BADLOCATION;

  fpSerial.listen();
  delay(50);

  Serial.print(">> Enroll ID "); Serial.println(id);
  lcd.clear(); lcd.setCursor(0,0);
  lcd.print("Enroll ID:");
  lcd.print(id);
  delay(400);

  // delete existing template to avoid conflicts
  finger.deleteModel(id);
  delay(200);

  unsigned long tstart;
  int res;

  // 1: first image
  lcd.clear(); lcd.setCursor(0,0); lcd.print("Place finger 1");
  tstart = millis();
  while(true){
    res = finger.getImage();
    if(res == FINGERPRINT_OK) break;
    if(res == FINGERPRINT_NOFINGER){
      if(millis()-tstart > FP_STAGE_TIMEOUT) return FINGERPRINT_TIMEOUT;
      delay(150);
      continue;
    } else {
      return res;
    }
  }
  if (finger.image2Tz(1) != FINGERPRINT_OK) return FINGERPRINT_IMAGEMESS;
  delay(300);

  // 2: second image
  lcd.clear(); lcd.setCursor(0,0); lcd.print("Remove finger");
  delay(1200);
  lcd.clear(); lcd.setCursor(0,0); lcd.print("Place finger 2");
  tstart = millis();
  while(true){
    res = finger.getImage();
    if(res == FINGERPRINT_OK) break;
    if(res == FINGERPRINT_NOFINGER){
      if(millis()-tstart > FP_STAGE_TIMEOUT) return FINGERPRINT_TIMEOUT;
      delay(150);
      continue;
    } else {
      return res;
    }
  }
  if (finger.image2Tz(2) != FINGERPRINT_OK) return FINGERPRINT_IMAGEMESS;
  delay(300);

  // create model
  if (finger.createModel() != FINGERPRINT_OK) return FINGERPRINT_PACKETRECIEVEERR;
  delay(200);

  // store model at 'id'
  if (finger.storeModel(id) != FINGERPRINT_OK) return FINGERPRINT_PACKETRECIEVEERR;

  espSerial.listen();
  delay(50);
  return FINGERPRINT_OK;
}

bool deleteFingerprintModel(int id) {
  if(id <= 0 || id >= MAX_TEMPLATE_ID) return false;
  fpSerial.listen();
  delay(40);
  int res = finger.deleteModel(id);
  espSerial.listen();
  delay(20);
  return (res == FINGERPRINT_OK);
}

int searchFingerprintOnce(int timeoutMs) {
  fpSerial.listen();
  delay(20);
  unsigned long st = millis();
  while(millis() - st < (unsigned long)timeoutMs){
    int p = finger.getImage();
    if(p == FINGERPRINT_OK){
      if(finger.image2Tz(1) == FINGERPRINT_OK){
        if(finger.fingerFastSearch() == FINGERPRINT_OK){
          int fid = finger.fingerID;
          espSerial.listen();
          delay(10);
          return fid;
        }
      }
    } else if(p == FINGERPRINT_NOFINGER){
      // continue waiting
    } else {
      // other error
      break;
    }
    delay(80);
  }
  espSerial.listen();
  delay(10);
  return -1;
}

// --- Command processing from ESP32 ---
String espInBuffer = "";
void processEspCommand(String cmd){
  cmd.trim();
  if(cmd.length() == 0) return;

  Serial.print("[UNO] Command from ESP32: "); Serial.println(cmd);

  if(cmd.startsWith("ENROLL:")){
    int id = cmd.substring(7).toInt();
    if(id <= 0 || id >= MAX_TEMPLATE_ID){
      espSerial.println("FP_FAIL");
      return;
    }
    lcd.clear(); lcd.setCursor(0,0); lcd.print("Enroll start");
    uint8_t r = getFingerprintEnroll(id);
    if(r == FINGERPRINT_OK){
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Enroll OK");
      espSerial.println("EDIT_DONE");   // EDIT_DONE indicates fp enrolled for id
    } else {
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Enroll FAIL");
      Serial.print("Enroll err: "); Serial.println(r);
      espSerial.println("FP_FAIL");
    }
    delay(400);
    return;
  } else if(cmd.startsWith("DELETE_FP:")){
    int id = cmd.substring(10).toInt();
    bool ok = deleteFingerprintModel(id);
    if(ok){
      espSerial.println("DELETE_DONE");
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Delete FP OK");
    } else {
      espSerial.println("DELETE_FAIL");
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Delete FP FAIL");
    }
    delay(300);
    return;
  } else if(cmd.startsWith("REQ_FP_SCAN")){
    int foundId = searchFingerprintOnce(2000); // 2s
    if(foundId >= 0){
      espSerial.print("FP_ID:");
      espSerial.println(foundId);
    } else {
      espSerial.println("FP_FAIL");
    }
    return;
  } else {
    Serial.println("[UNO] Unknown cmd");
    return;
  }
}

void setup() {
  Serial.begin(115200);       // Debug máy tính
  espSerial.begin(9600);      // Giao tiếp với ESP32
  SPI.begin();
  rfid.PCD_Init();
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("Ready to scan");

  // Init fingerprint sensor via fpSerial at 9600 (SoftwareSerial reliable)
  fpSerial.begin(57600);
  finger.begin(57600);
  fpSerial.listen();
delay(100);
  if (finger.verifyPassword()) {
    Serial.println("Found fingerprint sensor!");
  } else {
    Serial.println("Fingerprint sensor not found :(");
    lcd.clear(); lcd.setCursor(0,0); lcd.print("FP not found");
  }
  // ensure espSerial is listening by default
  espSerial.listen();
}

String readLineFromStream(Stream &s, unsigned long timeoutMs = 2000){
  String buf = "";
  unsigned long start = millis();
  while(millis() - start < timeoutMs){
    while(s.available()){
      char c = s.read();
      if(c == '\n'){
        buf.trim();
        return buf;
      } else if(c != '\r'){
        buf += c;
      }
    }
    delay(5);
  }
  buf.trim();
  return buf;
}

void loop() {
  // 1) check incoming commands from ESP32
  if(espSerial.available()){
    String line = readLineFromStream(espSerial, 150);
    if(line.length()>0){
      processEspCommand(line);
    }
  }

  // 2) normal RFID scanning
  if(rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    uidToSend = uidBytesToString(rfid.uid);
    uidToSend.toUpperCase();

    Serial.println("[UNO] UID: " + uidToSend);
    espSerial.println(uidToSend); // gửi ESP32

    lcd.clear();
    lcd.setCursor(0,0);

    if(uidToSend == ADMIN_UID){
      lcd.print("Chao Admin!");
      delay(3000);
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("Ready to scan");
      return;
    }

    lcd.print("Checking...");

    // Chờ phản hồi ESP32
    unsigned long startTime = millis();
    response = "";
    while(millis() - startTime < 2000){
      if(espSerial.available()){
        char c = espSerial.read();
        if(c == '\n'){
          response.trim();
          break;
        } else if(c != '\r'){
          response += c;
        }
      }
    }

    lcd.clear();
    if(response == "OK"){
      lcd.setCursor(0,0);
      lcd.print("Check-in OK");
      lcd.setCursor(0,1);
      lcd.print(uidToSend);
    } else if(response == "FAIL"){
      lcd.setCursor(0,0);
      lcd.print("Check-in FAIL");
    } else if(response == "ADMIN"){
      lcd.setCursor(0,0);
      lcd.print("Chao Admin");
    } else if(response == "REGISTER_DONE"){
      lcd.setCursor(0,0);
      lcd.print("Dang ky OK");
    } else if(response == "EDIT_DONE"){
      lcd.setCursor(0,0);
      lcd.print("Doi UID OK");
    } else if(response == "DELETE_DONE"){
      lcd.setCursor(0,0);
      lcd.print("Xoa OK");
    } else if(response.startsWith("FP_")) {
      lcd.setCursor(0,0);
      lcd.print(response);
    }

    delay(2500);
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Ready to scan");
  }

  // 3) fingerprint passive scanning (non-blocking quick check)
  fpSerial.listen();
  int p = finger.getImage();
  if(p == FINGERPRINT_OK){
    if(finger.image2Tz(1) == FINGERPRINT_OK){
      if(finger.fingerFastSearch() == FINGERPRINT_OK){
        int foundId = finger.fingerID;
        espSerial.print("FP_ID:");
        espSerial.println(foundId);

        // wait for response from ESP32 (OK/FAIL)
        unsigned long startTime = millis();
        response = "";
        while(millis() - startTime < 2000){
          if(espSerial.available()){
            char c = espSerial.read();
            if(c == '\n'){
              response.trim();
              break;
            } else if(c != '\r'){
              response += c;
            }
          }
        }

        lcd.clear();
        if(response == "OK"){
          lcd.setCursor(0,0); lcd.print("Finger OK id:");
          lcd.setCursor(0,1); lcd.print(foundId);
        } else {
          lcd.setCursor(0,0); lcd.print("Finger not reg");
        }
        delay(2000);
        lcd.clear(); lcd.setCursor(0,0); lcd.print("Ready to scan");
      }
    }
  }
  espSerial.listen();
  delay(30);
}
