#include <ESP8266WiFi.h>
#include <espnow.h>


uint8_t receiverAddress[] = {0x78, 0x1C, 0x3C, 0xCB, 0x26, 0xB8};


typedef struct struct_message {
  char status[20];      
  float timp;          
  float viteza;         
  unsigned long timestamp;
} struct_message;


typedef struct struct_mode {
  bool energyMode;      
} struct_mode;

struct_message myData;
struct_mode receivedMode;
volatile bool currentMode = false; 


const int pinSensor = 14; 
const float distanta = 1.0; 


volatile bool flagStart = false;
volatile bool flagStop = false;
volatile unsigned long timpStart = 0;
volatile unsigned long timpStop = 0;
volatile unsigned long ultimulTrigger = 0;


void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  Serial.print("Status trimitere: ");
  Serial.println(sendStatus == 0 ? "Succes" : "Eroare");
}


void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len) {
  if (len == sizeof(struct_mode)) {
    memcpy(&receivedMode, incomingData, sizeof(receivedMode));
    currentMode = receivedMode.energyMode;
    
    Serial.print("Mod schimbat la: ");
    Serial.println(currentMode ? "ENERGY" : "SPEED");
  } else {
    Serial.println("Dimensiune pachet invalidă primită, ignorat");
  }
}

void ICACHE_RAM_ATTR ISR_Sensor() {
  unsigned long now = millis();
 
  if (currentMode) {
    if (now - ultimulTrigger > 300) {
      flagStart = true; // Folosim flagStart pentru a semnala impuls
      ultimulTrigger = now;
    }
  }
  // În Speed Mode, primul impuls = START, al doilea = STOP
  else {
    // Primul impuls = START
    if (!flagStart && (now - ultimulTrigger > 300)) {
      timpStart = now;
      flagStart = true;
      ultimulTrigger = now;
      Serial.println("START detectat");
    }
    // Al doilea impuls = STOP
    else if (flagStart && !flagStop && (now - ultimulTrigger > 300)) {
      timpStop = now;
      flagStop = true;
      ultimulTrigger = now;
      Serial.println("STOP detectat");
    }
  }
}

void trimiteDateESPNOW() {
  esp_now_send(receiverAddress, (uint8_t *) &myData, sizeof(myData));
}

void setup() {
  Serial.begin(115200);
  
  pinMode(pinSensor, INPUT);
  attachInterrupt(digitalPinToInterrupt(pinSensor), ISR_Sensor, RISING);
  
  // Configurare WiFi în modul Station
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  // Inițializare ESP-NOW
  if (esp_now_init() != 0) {
    Serial.println("Eroare ESP-NOW init");
    return;
  }
  
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv); // Primește notificări de mod
  
  // Adaugă peer
  esp_now_add_peer(receiverAddress, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);
  
  Serial.println("ESP8266 Sender - Ver 6.1 (Bidirectional)");
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());
  
  resetSistem();
}

void loop() {
  // ENERGY MODE: Orice impuls trimite RESULT imediat
  if (currentMode) {
    if (flagStart) { // În Energy Mode folosim doar flagStart
      strcpy(myData.status, "RESULT");
      myData.timp = 0;
      myData.viteza = 0;
      myData.timestamp = millis();
      trimiteDateESPNOW();
      
      Serial.println("Impuls Energy Mode trimis!");
      
      delay(270);
      resetSistem();
    }
  }
  // SPEED MODE: Primul impuls START, al doilea STOP
  else {
    // START - afișează cronometru
    if (flagStart && !flagStop) {
      strcpy(myData.status, "TIMING");
      myData.timp = (millis() - timpStart) / 1000.0;
      myData.viteza = 0;
      myData.timestamp = millis();
      trimiteDateESPNOW();
      delay(280);
    }
    
    // STOP & CALCUL - al doilea impuls
    if (flagStop) {
      float secunde = (timpStop - timpStart) / 1000.0;
      
      if (secunde > 0.05) {
         float viteza = (distanta / secunde) * 3.6;
         
         strcpy(myData.status, "RESULT");
         myData.timp = secunde;
         myData.viteza = viteza;
         myData.timestamp = millis();
         trimiteDateESPNOW();
         
         Serial.print("Timp: "); Serial.print(secunde, 2); Serial.println("s");
         Serial.print("Viteza: "); Serial.print(viteza, 1); Serial.println(" km/h");
         
         delay(10000);
         resetSistem();
      } else {
         Serial.println("Timp prea scurt, resetare");
         resetSistem();
      }
    }
    
    // TIMEOUT - dacă nu vine al doilea impuls în 10 secunde
    if (flagStart && !flagStop && (millis() - timpStart > 10000)) {
       Serial.println("Timeout - nu s-a primit al doilea impuls");
       resetSistem();
    }
  }
}

void resetSistem() {
  flagStart = false;
  flagStop = false;
  
  strcpy(myData.status, "WAITING");
  myData.timp = 0;
  myData.viteza = 0;
  myData.timestamp = millis();
  trimiteDateESPNOW();
  
  Serial.println("Astept pieton...");
}
