#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>
#include <SoftwareSerial.h>

#define RST_PIN 9
#define SS_PIN 10

MFRC522 rfid(SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(0x27,16,2);

// Chân giao tiếp với ESP32
#define RX_PIN 2
#define TX_PIN 3
SoftwareSerial espSerial(RX_PIN, TX_PIN); // RX, TX

String uidToSend = "";
String response = "";

// Admin UID
const String ADMIN_UID = "54:4D:C9:05"; // Thẻ Admin

void setup() {
  Serial.begin(115200);       // Debug máy tính
  espSerial.begin(9600);      // Giao tiếp với ESP32
  SPI.begin();
  rfid.PCD_Init();
  a
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("Ready to scan");
}

void loop() {
  if(rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
   uidToSend = "";
for(byte i=0;i<rfid.uid.size;i++){
  if(i!=0) uidToSend += ":";
  if(rfid.uid.uidByte[i] < 0x10) uidToSend += "0";  // thêm 0 nếu < 16
  uidToSend += String(rfid.uid.uidByte[i], HEX);
}
uidToSend.toUpperCase();


    Serial.println("[UNO] UID: " + uidToSend);
    espSerial.println(uidToSend); // gửi ESP32

    lcd.clear();
    lcd.setCursor(0,0);

    // Nếu là admin
    if(uidToSend == ADMIN_UID){
      lcd.print("Chao Admin!");
      delay(3000);
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("Ready to scan");
      return; // không tiếp tục check-in
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
        } else {
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
    }

    delay(3000);
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Ready to scan");
  }
}
