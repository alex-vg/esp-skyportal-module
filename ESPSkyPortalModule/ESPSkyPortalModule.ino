// https://github.com/espressif/arduino-esp32/tree/master/libraries/WiFi/src

//#define DEBUG
//#define DEBUG_INOUT
//#define DEBUG_CRAZY

#include <WiFi.h>
#include <WiFiUdp.h>

#define RXPIN 22
#define TXPIN 23
#define SELPIN 21
#define LEDPIN 2

// Replace with your network credentials
const char* ssid     = "Celestron-F7F";
const char* password = "123456789";

// advertisement over udp. interestingly, the mac advertised starts with 4C, but the actual mac is starting with 4E.
// the end "0F:80" is (0x0F, 0x7F) +1 because WiFi.h uses a diffrent MAC (mac+1) for the server than for wifi advertisment.
const char* advertisment = "{\"mac\":\"4C:55:CC:1A:0F:80\",\"version\":\"ZENTRI-AMW007-1.2.0.10, 2017-07-28T07:43:27Z, ZentriOS-WL-1.2.0.10\"}";

uint8_t mac[] = {0x4E, 0x55, 0xCC, 0x1A, 0x0F, 0x7F};

// tcp 2000
WiFiServer tcpServer(2000);
#define TCPCLIENTMAX 5
WiFiClient tcpClients[TCPCLIENTMAX];
WiFiClient newTcpClient;

// create UDP instance
WiFiUDP udp;

// skyPortal connects to 1.2.3.4:2000
IPAddress local_IP(1, 2, 3, 4);
IPAddress gateway(1, 2, 3, 4);
IPAddress subnet(255, 255, 255, 0);

IPAddress broadcast_IP(255, 255, 255, 255);
uint16_t broadcastToPort=55555;

#define BUFLENGTH 64
char output[BUFLENGTH];

volatile boolean transmission = false;

uint16_t counter = 0;

#define AdLoop 100000
#define LoopTimout 180

void sendAd2() {
    udp.beginPacket(broadcast_IP, broadcastToPort);
    udp.printf("%s", advertisment);
    udp.endPacket();
    #ifdef DEBUG
      Serial.print("-");
    #endif
}

void sendAd() {
    udp.beginPacket(broadcast_IP, broadcastToPort);
    udp.printf("%s", advertisment);
    udp.endPacket();
    #ifdef DEBUG
      Serial.print(".");
    #endif
}

#ifdef DEBUG_INOUT
void debugBufHex(char* buf, unsigned int leng) {
  for(unsigned int i=0; i<leng; i++) {
    if(i==2) {
      Serial.print("[");
    }
    if(buf[i] < 0x10) {
      Serial.print(0, HEX);
    }
    Serial.print(buf[i], HEX);
    if(i==leng-2) {
      Serial.print("]");
    }
    Serial.print(" ");
  }
}

void debugBuf(const char* msg, char *buf) {
  if(msg != NULL) {
    Serial.print(msg);
    Serial.print(" ");
  }
  debugBufHex(buf, BUFLENGTH);
  Serial.print("\n");
}
#endif

void prepareSSWrite() {
  pinMode(TXPIN, OUTPUT);
  digitalWrite(TXPIN, HIGH);
}

void prepareSSRead() {
  pinMode(RXPIN, INPUT_PULLUP);
}

void WiFiEventConfigureWifi(WiFiEvent_t event, WiFiEventInfo_t info) {
  // configure...
  #ifdef DEBUG
    Serial.println(WiFi.softAPConfig(local_IP, gateway, subnet) ? "Configured" : "Failed to configure!");
  #else
    WiFi.softAPConfig(local_IP, gateway, subnet);
  #endif
  IPAddress IP = WiFi.softAPIP();
  #ifdef DEBUG
    Serial.print("AP IP address: ");
    Serial.println(IP);
  #endif
}

bool waitForSelect(unsigned long timeout) {
  unsigned long startMillis = millis();
  while(digitalRead(SELPIN)==LOW && millis() - startMillis < timeout);
  return (digitalRead(SELPIN)==HIGH);
}

size_t readAnyStream(Stream *inputStream, const char* descr, char* buffer) {
  size_t leng;
  size_t totalLeng;
  size_t readLeng;
  inputStream->setTimeout(15);
  if(inputStream->find(0x3B)) { // timeout returns false after Serial2.setTimout..
    leng = inputStream->read();
    totalLeng = leng+3;
    buffer[0]=0x3B;
    buffer[1]=leng;
    readLeng = inputStream->readBytes(&buffer[2], leng+1);
    if(readLeng != leng+1) {
      #ifdef DEBUG
        Serial.print(descr);
        Serial.print(" got ");
        Serial.print(readLeng);
        Serial.print(" but expected ");
        Serial.print(leng+1);
        Serial.println(" bytes. (timeout)");
      #endif
    }
    return readLeng+2;
  }
  return 0;
}

void writeAllStreams(WiFiClient outputStreams[], size_t streamsSize, size_t leng, const char* descr, char* buffer) {
  for(size_t i=0; i<streamsSize; i++) {
    if (!outputStreams[i].connected()) {
      continue;
    }
    char *desc2 = (char*)malloc(sizeof(descr)+4);
    sprintf(desc2,"%s(%i)",descr,i);
    writeAnyStream(&outputStreams[i], leng, desc2, buffer);
  }
}

void writeAnyStream(Stream *outputStream, size_t leng, const char* descr, char* buffer) {
  outputStream->write(buffer, leng);
  #ifdef DEBUG_INOUT
    Serial.print(descr);
    Serial.print(" (");
    Serial.print(leng);
    Serial.print("): ");
    debugBufHex(buffer, leng);
    Serial.println("");
  #endif
}

void serialEvent2(WiFiClient tcpClients[]) {
  size_t leng = readAnyStream(&Serial2, "from NS ", output);
  if (leng > 2 && output[2] == 0x20) {
    #ifdef DEBUG_INOUT
      Serial.print("from NS detected echo. ");
      debugBufHex(output, leng);
      Serial.println("");
    #endif
    serialEvent2(tcpClients);
    return;
  }
  if(leng > 0) {
    writeAllStreams(tcpClients, TCPCLIENTMAX, leng, "from NS ", output);
  }
}

void tcpEvent(WiFiClient *tcpClient) {
  size_t leng = readAnyStream(tcpClient, "from TCP", output);
  if(leng > 0) {
    if(!waitForSelect(600)) {
      // can't write... NS is busy
      return;
    }
    setSelOutput();
    delayMicroseconds(400);
    writeAnyStream(&Serial2, leng, "from TCP", output);
    delayMicroseconds(400);
    Serial2.flush();
    setSelInput();
  }
}

void serial_ISR() {
  transmission = true;
}

void setSelOutput() {
  detachInterrupt(digitalPinToInterrupt(SELPIN));
  pinMode(SELPIN, OUTPUT);
  digitalWrite(SELPIN, LOW);
}

void setSelInput() {
  pinMode(SELPIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(SELPIN), serial_ISR, FALLING);
}

void setup() {
  esp_base_mac_addr_set(&mac[0]);
  Serial.begin(115200);
  Serial2.setTimeout(1000);
  tcpServer.setTimeout(1000);

  // Connect to Wi-Fi network with SSID and password
  Serial.println("Setting AP (Access Point)...");

  WiFi.onEvent(WiFiEventConfigureWifi, WiFiEvent_t::SYSTEM_EVENT_AP_START);

  // Add the password parameter, if you want the AP (Access Point) to be open
  // WiFi.softAP(ssid, password);
  WiFi.softAP(ssid, password);
  Serial.print("WiFi Mac: ");
  Serial.println(WiFi.macAddress());

  tcpServer.begin();
  tcpServer.setNoDelay(true);
  //tcpServer.setTimeout(5);

  Serial2.begin(19200, SERIAL_8N2, RXPIN, TXPIN);
  prepareSSWrite();
  prepareSSRead();
  setSelInput();
}

void loop(){
  counter++;
  if (counter % AdLoop == 0) {
    sendAd2();
  }

  newTcpClient = tcpServer.available();


  if(newTcpClient) {
    for(uint8_t i=0; i<TCPCLIENTMAX; i++) {
      if(!tcpClients[i].connected()) {
        tcpClients[i] = newTcpClient;
        #ifdef DEBUG
          Serial.print("Connect on 2000! (");
          Serial.print(i);
          Serial.println(")");
        #endif
        break;
      }
    }
  }
  

  for(uint8_t i=0; i<TCPCLIENTMAX; i++) {                             // If a new client connects,
    if(!tcpClients[i].connected()) {
      continue;
    }

    if (tcpClients[i].connected()) {

      if (tcpClients[i].available()) {             // if there's bytes to read from the client,
        tcpEvent(&tcpClients[i]);
      }

      if(Serial2.available() || transmission) {
        noInterrupts();
        transmission = false;
        serialEvent2(tcpClients);
        interrupts();
      }
    } else {
      // Close the connection
      tcpClients[i].stop();
  
      #ifdef DEBUG
        Serial.print("Connection lost. (");
        Serial.print(i);
        Serial.println(")");
      #endif
    }
  }
}
