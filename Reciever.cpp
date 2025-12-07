#include <WiFi.h>
#include <esp_now.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);


typedef struct struct_message {
  char status[20];
  float timp;
  float viteza;
  unsigned long timestamp;
} struct_message;

struct_message receivedData;
unsigned long lastReceived = 0;

// Structură pentru trimitere mod înapoi la ESP8266
typedef struct struct_mode {
  bool energyMode;
} struct_mode;

struct_mode modeData;
uint8_t senderAddress[6]; // Vom stoca MAC-ul sender-ului

// Variabile pentru comutare mod
const int buttonPin = 23; // 23 - Buton de comutare
bool displayMode = false;  // false = Speed Mode, true = Energy Mode
volatile bool buttonPressed = false;
volatile unsigned long lastButtonPress = 0;

// Variabile pentru contor impulsuri
float totalJoules = 0.0;

// ISR pentru buton
void IRAM_ATTR ISR_Button() {
  if (millis() - lastButtonPress > 300) { // Debounce 300ms
    buttonPressed = true;
    lastButtonPress = millis();
  }
}

// Callback primire date (ESP32 v3.x)
void OnDataRecv(const esp_now_recv_info *recv_info, const uint8_t *incomingData, int len) {
  // Verifică că primim structura corectă
  if (len == sizeof(struct_message)) {
    memcpy(&receivedData, incomingData, sizeof(receivedData));
    lastReceived = millis();
    
    // Salvează adresa sender-ului pentru răspuns
    memcpy(senderAddress, recv_info->src_addr, 6);
    
    Serial.print("Date primite - Status: ");
    Serial.println(receivedData.status);
    
    // Incrementează contor când primim rezultat
    if (strcmp(receivedData.status, "RESULT") == 0) {
      totalJoules += 1.4; 
      
      Serial.print("Impuls primit! Total: ");
      Serial.println(totalJoules, 0);
    }
    
    updateDisplay();
  } else {
    Serial.print("Pachet invalid primit, dimensiune: ");
    Serial.println(len);
  }
}

// Funcție pentru trimitere mod către ESP8266
void sendModeToSender() {
  modeData.energyMode = displayMode;
  
  // Verifică dacă avem peer-ul adăugat
  if (esp_now_is_peer_exist(senderAddress)) {
    esp_now_send(senderAddress, (uint8_t *) &modeData, sizeof(modeData));
  } else {
    // Adaugă peer dacă nu există
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, senderAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) == ESP_OK) {
      esp_now_send(senderAddress, (uint8_t *) &modeData, sizeof(modeData));
      Serial.println("Peer adăugat și mod trimis");
    }
  }
  
  Serial.print("Mod trimis către sender: ");
  Serial.println(displayMode ? "ENERGY" : "SPEED");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Configurare buton
  pinMode(buttonPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(buttonPin), ISR_Button, FALLING);
  
  // Inițializare LCD
  Wire.begin(21, 22); // SDA=21, SCL=22 (ESP32 default)
  lcd.init();
  lcd.backlight();
  
  lcd.setCursor(0,0);
  lcd.print("Dashboard ESP32");
  lcd.setCursor(0,1);
  lcd.print("Dual Mode v1.0");
  delay(1500);
  
  // Configurare WiFi
  WiFi.mode(WIFI_STA);
  
  Serial.println("\n=============================");
  Serial.println("ESP32 RECEIVER DASHBOARD");
  Serial.println("DUAL MODE: Speed / Energy");
  Serial.println("=============================");
  
  // Inițializare ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Eroare ESP-NOW init");
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Eroare ESP-NOW!");
    return;
  }
  
  // Înregistrare callback
  esp_now_register_recv_cb(OnDataRecv);
  
  Serial.println("ESP-NOW initialized");
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());
  Serial.println("Buton pe GPIO23");
  Serial.println("=============================\n");
  
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Receiver Ready");
  lcd.setCursor(0,1);
  lcd.print("Mode: SPEED");
  delay(2000);
  
  updateDisplay();
}

void loop() {
  // Verifică dacă s-a apăsat butonul
  if (buttonPressed) {
    buttonPressed = false;
    displayMode = !displayMode; // Comută modul
    
    Serial.print("Mod comutat la: ");
    Serial.println(displayMode ? "ENERGY" : "SPEED");
    
    // Trimite noul mod către ESP8266
    sendModeToSender();
    
    // Afișare mesaj comutare
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Mod schimbat:");
    lcd.setCursor(0,1);
    lcd.print(displayMode ? ">>> ENERGY <<<" : ">>> SPEED <<<");
    delay(1000);
    
    updateDisplay();
  }
  
  // Verifică timeout conexiune (15 secunde fără date)
  if (lastReceived > 0 && (millis() - lastReceived > 15000)) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Conexiune");
    lcd.setCursor(0,1);
    lcd.print("Pierduta!");
    delay(2000);
    
    updateDisplay();
    lastReceived = 0;
  }
  
  delay(50);
}

void updateDisplay() {
  lcd.clear();
  
  // MOD SPEED (original)
  if (!displayMode) {
    if (strcmp(receivedData.status, "WAITING") == 0 || lastReceived == 0) {
      lcd.setCursor(0,0);
      lcd.print("Astept Pieton...");
      lcd.setCursor(0,1);
      lcd.print("[SPEED Mode]");
    }
    else if (strcmp(receivedData.status, "TIMING") == 0) {
      lcd.setCursor(0,0);
      lcd.print("-> CRONOMETRU");
      lcd.setCursor(0,1);
      lcd.print("Timp: ");
      lcd.print(receivedData.timp, 1);
      lcd.print("s");
    }
    else if (strcmp(receivedData.status, "RESULT") == 0) {
      lcd.setCursor(0,0);
      lcd.print("Timp: ");
      lcd.print(receivedData.timp, 2);
      lcd.print("s");
      
      lcd.setCursor(0,1);
      lcd.print("Vit: ");
      lcd.print(receivedData.viteza, 1);
      lcd.print(" km/h");
    }
  }
  // MOD ENERGY (contor jouli)
  else {
    lcd.setCursor(0,0);
    lcd.print("Putere produsa:");
    lcd.setCursor(0,1);
    
    // Afișare simplu număr întreg
    lcd.print((int)totalJoules);
    lcd.print(" Jouli");
  }
}
