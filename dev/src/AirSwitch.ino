// tester les schémas de stabilisation du esp12 avec condo!
// ------------------------------------ Initialisation --------------------------------------------------

// ----------------- Librairies
  #include <Arduino.h>
  #include <Hash.h>
  #include <WebSocketsServer.h>
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  #include <ESP8266mDNS.h>
  #include <FS.h>
  #include <dht11.h> // the DHT11 needs power to read temperature and humidity, the arduino 3.3v is not sufficent
  #include "user_interface.h" //for timer purpose
  #include <Average.h>
  #include "aes256.h" //Crypto library

// ----------------- DHT definition
  dht11 DHT;
  #define DHT11_PIN 10

// ----------------- Variables definition
  Average<float> ave(135); // // Reserve space for 135 entries in the average bucket.
  aes256_context ctxt; // Crypto init

  String wifistatestring[7] = {"Inactive", "No SSID available", "Scan completed", "Connected", "Connection failed", "Connection lost", "Connecting"};
  const char* initSTssid = "NUMERICABLE-3DA4";
  const char* initSTpwd = "BJX57NGKBW"; // Reading file error on STssid
  const char* initAPssid = "Light Switch";
  const char* initAPpwd = "12345678";
  //const char* homeip = "192.168.0.11"; // host must be live updated and homeip != HomeIP ?
  String APssid;
  String APpwd;
  String STssid;
  String STpwd;
  String WifiLastStatus;
  String InfoFile = "/info.txt";
  String HomeFile = "/home.txt";
  String HomeState = "Not connected";
  String HomeIP;
  boolean WifiActive = false;
  boolean homeClientConnected = false;
  boolean waitingClient=false;
  boolean needUpdate = false;
  float oldAmp;
  os_timer_t myTimer1;
  os_timer_t myTimer2;
  os_timer_t myTimer3;
  bool tickUpdate;
  bool tickDHT;
  bool tickCurrent;

  unsigned long lastDhtRead=0;
  String oldtempDht;
  String oldhumDht;
  String tempDht;
  String humDht;
  float currentValue;
  int val[200] ; // Current raw data

  const int ledRed = 4;      // the number of the LED pins
  const int ledGreen = 0;
  const int ledBlue = 2;

  // color = [black,red,green,blue,cyan,purple,yellow,white]
  // pwm is flickering when using more than 2 outputs at the same time. Edit : no flickering when current is enough! 
  const int color[8][3]={{0,0,0},{500,0,0},{0,500,0},{0,0,500},{0,500,500},{300,0,500},{300,500,0},{300,500,500}};

  const char* host = "switch";
  uint8_t MAC_array[6];
  char MAC_char[18];
  const int R1PIN = 5;
  const int R2PIN = 14;
  const int R3PIN = 12;
  const int R4PIN = 13;
  bool R1Status;
  bool R2Status;
  bool R3Status;
  bool R4Status;
  char myIPString[24];
  char mylocalIPString[24];
  char mysubnetMaskString[24];
  char mygatewayIPString[24];
  int chk; // State of the DHT11 sensor (values are more reliable when chk is declared outside the voids)
  int clientTimeout=0;

  WiFiClient homeclient;
  String homeurl = "/api/esps";
  int homeport = 8080;

  // Commands sent through Web Socket
  const char R1ON[] = "r1on\n";
  const char R1OFF[] = "r1off\n";
  const char R2ON[] = "r2on\n";
  const char R2OFF[] = "r2off\n";
  const char R3ON[] = "r3on\n";
  const char R3OFF[] = "r3off\n";
  const char R4ON[] = "r4on\n";
  const char R4OFF[] = "r4off\n";

// ----------------- Servers definition
  ESP8266WebServer server(80);
  WebSocketsServer webSocket = WebSocketsServer(81);


//------------------------------SPIFFS Files system sub-routines-----------------------------------------
  String formatBytes(size_t bytes) {
    if (bytes < 1024) {
      return String(bytes) + "B";
    } else if (bytes < (1024 * 1024)) {
      return String(bytes / 1024.0) + "KB";
    } else if (bytes < (1024 * 1024 * 1024)) {
      return String(bytes / 1024.0 / 1024.0) + "MB";
    } else {
      return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";
    }
  }

  String getContentType(String filename) {
    if (server.hasArg("download")) return "application/octet-stream";
    else if (filename.endsWith(".htm")) return "text/html";
    else if (filename.endsWith(".html")) return "text/html";
    else if (filename.endsWith(".css")) return "text/css";
    else if (filename.endsWith(".js")) return "application/javascript";
    else if (filename.endsWith(".png")) return "image/png";
    else if (filename.endsWith(".gif")) return "image/gif";
    else if (filename.endsWith(".jpg")) return "image/jpeg";
    else if (filename.endsWith(".ico")) return "image/x-icon";
    else if (filename.endsWith(".xml")) return "text/xml";
    else if (filename.endsWith(".pdf")) return "application/x-pdf";
    else if (filename.endsWith(".zip")) return "application/x-zip";
    else if (filename.endsWith(".gz")) return "application/x-gzip";
    return "text/plain";
  }

  bool handleFileRead(String path) {
    //Serial.println("handleFileRead: " + path);
    if (path.endsWith("/")) path += "index.htm";
    String contentType = getContentType(path);
    String pathWithGz = path + ".gz";
    if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {
      if (SPIFFS.exists(pathWithGz))
        path += ".gz";
      File file = SPIFFS.open(path, "r");
      size_t sent = server.streamFile(file, contentType);
      file.close();
      return true;
    }
    return false;
  }

  void handleFileList() {
    //if(!server.hasArg("dir")) {server.send(500, "text/plain", "BAD ARGS"); return;}

    //String path = server.arg("dir");
    Serial.println("handleFileList: ");
    Dir dir = SPIFFS.openDir("/");
    //path = String();

    String output = "[";
    while (dir.next()) {
      File entry = dir.openFile("r");
      if (output != "[") output += ',';
      bool isDir = false;
      output += "{\"type\":\"";
      output += (isDir) ? "dir" : "file";
      output += "\",\"name\":\"";
      output += String(entry.name()).substring(1);
      output += "\"}";
      entry.close();
    }

    output += "]";
    server.send(200, "text/json", output);
  }

//------------------------------ Internal sub-routines ---------------------------------------------------

//----------------  Sensors
  void RGB(int colorNum){
   analogWrite(ledRed,color[colorNum][0]);
   analogWrite(ledGreen,color[colorNum][1]);
   analogWrite(ledBlue,color[colorNum][2]);
   //needUpdate = true; we cant use the same rgb() for api and for sendDataClient
  }

  void writeRelay(int RelayNum, bool Rstate) {    // Note inverted logic depending on relay type
    switch (RelayNum) {
        case 1:
        R1Status = Rstate;
          if (Rstate) {
            digitalWrite(R1PIN, LOW);
          }
          else {
            digitalWrite(R1PIN, HIGH);
          }
        break;
        
        case 2:
        R2Status = Rstate;
          if (Rstate) {
            digitalWrite(R2PIN, LOW);
          }
          else {
            digitalWrite(R2PIN, HIGH);
          }
        break;
        
        case 3:
        R3Status = Rstate;
          if (Rstate) {
            digitalWrite(R3PIN, LOW);
          }
          else {
            digitalWrite(R3PIN, HIGH);
          }
        break;
        
        case 4:
        R4Status = Rstate;
          if (Rstate) {
            digitalWrite(R4PIN, LOW);
          }
          else {
            digitalWrite(R4PIN, HIGH);
          }
        break;
    }
    needUpdate = true;
  }

  void currentRead() {

    int temp = 0;
    float newAmp = 0;
    long aveR;
    int val[135] ;
    ave.clear();
       
    // Add a new random value to the bucket
    
    for (int i = 0; i < 135; i++) {
    ave.push(analogRead(A0));
    }
    aveR=ave.mean();

    long powerSum=0;
          for (int i = 5 ; i < 135; i++) {
            powerSum+=abs(ave.get(i)-aveR);          
          }

    newAmp =0.00033553*powerSum+0.05324;

    // look if the value needs to be updated to the "home" server
    if (int(newAmp*10) != int(oldAmp*10)){
        needUpdate = true;
        Serial.println("current changed");
        oldAmp=newAmp;
    }
    //return the interpolation of the analogValue with the real current flowing (3 equations/3 areas)
    // value should be in a global variable
    currentValue=newAmp;
  }

  void dhtRead() {
    //2sec request delay if dht has been readen just before
    if(lastDhtRead+2000<millis()){
        chk = DHT.read(DHT11_PIN);    // READ DATA 
        switch (chk) {
          case DHTLIB_OK:
            tempDht = String(DHT.temperature);
            humDht = String(DHT.humidity);
            break;
          case DHTLIB_ERROR_CHECKSUM:
            tempDht = "error";
            humDht ="error";
            break;
          case DHTLIB_ERROR_TIMEOUT:
            tempDht = "error";
            humDht ="error";
            break;
          default:
            tempDht = "error";
            humDht ="error";
            break;
        }
    lastDhtRead=millis();
    if(tempDht!=oldtempDht || humDht!=oldhumDht){
      oldtempDht=tempDht;
      oldhumDht=humDht;
      needUpdate=true;
      Serial.println("dht updated");
    }
    }
  }

//------------ Internal file handling
  void readWifiData(String filename) {
    File f = SPIFFS.open(filename, "r");
    if (!f) {
      Serial.println("Wifi parameters file failed to open");
    }
    else {
      APssid = f.readStringUntil('\n');
      APssid = APssid.substring(0, APssid.length() - 1);
      APpwd = f.readStringUntil('\n');
      APpwd = APpwd.substring(0, APpwd.length() - 1);
      STssid = f.readStringUntil('\n');
      STssid = STssid.substring(0, STssid.length() - 1);
      STpwd = f.readStringUntil('\n');
      STpwd = STpwd.substring(0, STpwd.length() - 1);

      f.close();
    }

    if (APssid.length() < 1) { //we can add more security in text verifying
      APssid = initAPssid;
      APpwd = initAPpwd;
    }

    if (STssid.length() < 1) {
      STssid = initSTssid;
      STpwd = initSTpwd;
      Serial.println(STssid);
      Serial.println(STpwd);
    }
  }

  void WriteWifiData(String filename) {
    File f = SPIFFS.open(filename, "w");
    if (!f) {
      Serial.println("file open failed");
    }
    else {
      f.println(APssid);
      f.println(APpwd);
      f.println(STssid);
      f.println(STpwd);
      f.close();
      Serial.println("file writed");
    }
  }

  void WriteSerialData(String filename, String data) {
    File f = SPIFFS.open(filename, "w");
    if (!f) {
      Serial.println("file open failed");
    }
    else {
      f.println(data);
      f.close();
      Serial.println("file writed");
    } //writes serial data to a file
  }

  void readHomeData(String filename) {
    String Homeportstr;
    File f = SPIFFS.open(filename, "r");
    if (!f) {
      Serial.println("Home parameters file failed to open");
    }
    else {
      HomeIP = f.readStringUntil('\n');
      HomeIP = HomeIP.substring(0, HomeIP.length() - 1);
      Homeportstr = f.readStringUntil('\n');
      Homeportstr = Homeportstr.substring(0, Homeportstr.length() - 1);
      homeport=Homeportstr.toInt();
      f.close();
    }
  }

  void writeHomeData(String filename) {

    File f = SPIFFS.open(HomeFile, "w");
    if (!f) {
      Serial.println("file open failed");
    }
    else {
      f.println(HomeIP);
      f.println(homeport);
      f.close();
      Serial.println("file writed");
    }
  }

//----------------------------- String elaboration sub-routines -----------------------------------------
  String wifiscan() {
    Serial.println("scan start");

    // WiFi.scanNetworks will return the number of networks found
    // WiFi.scanNetworks doesnt work (miss always the best network) on this sdk version issue:#1355 on github
    // fixed : https://github.com/esp8266/Arduino/issues/1355

    // sort an array : http://www.hackshed.co.uk/arduino-sorting-array-integers-with-a-bubble-sort-algorithm/
    int n = WiFi.scanNetworks();
    Serial.println("scan done");
    if (n == 0)
      Serial.println("no networks found");
    else
    {
      Serial.print(n);
      Serial.println(" networks found");

      int rssi[30];
      memset(rssi, 0, sizeof(rssi));
      for (int i = 0; i < n; ++i) {
        rssi[i] = WiFi.RSSI(i);
      }

      int rank[30];
      for (int i = 0; i < 30; ++i) {
        rank[i] = i;
      }

      sort(rssi, n, rank);


      // Print SSID and RSSI in a json for each network found
      String json="[";
      for (int i = 0; i < n; ++i) {
        json += "{\"SSID\":\"" + String(WiFi.SSID(rank[i])) + "\"";
        json += ",\"RSSI\":\"" + String(WiFi.RSSI(rank[i])) + "\"},";
      }
      json = json.substring(0, json.length() - 1);
      json += "]";
      return json;
    }
    //Serial.println("");
  }

  String wifistate() {
    // add ssid name (if wifistatus=connecting or connected or not exist?)
    String json = "{\"apstate\":\"" + wifistatestring[WiFi.status()] + "\",{\"apname\":\"" + STssid + "\"}";
    return json;
  }

  String wifiparam() {

    WifiToVars();

    String json = "{\"APip\":\"" + String(myIPString) + "\"";
    json += ",\"APssid\":\"" + APssid + "\"";
          if (WifiActive == true) {
            json += ",\"state\":\"" + wifistatestring[WiFi.status()] + "\"";
            json += ",\"STssid\":\"" + String(WiFi.SSID()) + "\"";
            }
          else {
            json += ",\"state\":\"" + WifiLastStatus + "\"";
            json += ",\"STssid\":\"" + STssid + "\"";
          }
    json += ",\"localip\":\"" + String(mylocalIPString) + "\"";
    json += ",\"netmaskip\":\"" + String(mysubnetMaskString) + "\"";
    json += ",\"gatewayip\":\"" + String(mygatewayIPString) + "\"";
    json += ",\"mac\":\""  + String(MAC_char) +  "\"";
    json += ",\"homeip\":\""  + HomeIP +  "\"";
    json += ",\"homeport\":\""  + String(homeport) +  "\"";
    json += ",\"homestate\":\""  + HomeState +  "\"}";

    return json;
  }

  String macstr() {
    WiFi.macAddress(MAC_array);
    for (int i = 0; i < sizeof(MAC_array); ++i) {
      sprintf(MAC_char, "%s%02x:", MAC_char, MAC_array[i]);
    }
    MAC_char[17] = (char)0;
    return String(MAC_char);
  }

  String macaddress() {
    String json = "{\"mac address\":\"" + String(MAC_char) +  "\"}";
    return json;
  }

  String dhtjson() {
    String json = "{\"temperature\":\"" + tempDht +  "\",\"humidity\":\"" + humDht +  "\"}";
    return json;
  }

  String currentjson() {
    String json = "{\"current\":\"" + String(currentValue) +  "\"}";
    return json;
  }

  String iotDBjson() {
    String json = "{\"iotname\":\"" + APssid + "\",";
    json += "\"iotid\":\"" + String(MAC_char) + "\",";
    json += "\"r1\":\"" + String(R1Status) + "\",";
    json += "\"r2\":\"" + String(R2Status) + "\",";
    json += "\"r3\":\"" + String(R3Status) + "\",";
    json += "\"r4\":\"" + String(R4Status) + "\",";      
    json += "\"localip\":\"" + String(mylocalIPString) + "\",";
    json += "\"current\":\"" + String(currentValue) + "\",";
    json += "\"temperature\":\"" + tempDht + "\",";
    json += "\"humidity\":\"" + humDht + "\"}";
    return json;
  }

  String cryptojson(){
    String json; // = iotDBjson():
      uint8_t key[] ="1234567890ABCDEFGHIJKLMNOPQRSTUV";
      aes256_init(&ctxt, key);
      uint8_t data[] = "bonjour ca va?!!"; // I want to put here the json string
      aes256_encrypt_ecb(&ctxt, data); // must decrypt on receiving data too
      aes256_done(&ctxt);
      json = (char*)data;
      return json;
  }

  String loadjson() {
      String json="[";
      for (int i = 0; i < 200; i++) {
        val[i] = analogRead(A0);
        json +=String(val[i])+",";
      }
      json=json.substring(0, json.length() - 1);
      json+="]";
      return json;
  }

  String iotDBget() { // the old way of sending GET request
    String DBget = "?iotname=" + APssid;
    DBget += "&iotid=" + String(MAC_char);
    DBget += "&r1=" + String(R1Status);
    DBget += "&r2=" + String(R2Status);
    DBget += "&r3=" + String(R3Status);
    DBget += "&r4=" + String(R4Status);      
    DBget += "&localip=" + String(mylocalIPString);
    DBget += "&current=" + String(currentValue);
    DBget += "&temperature=" + tempDht;
    DBget += "&humidity=" + humDht;
    return DBget;
  }

  String iotDBnewget() { // the new way of sending GET request has this shape : HomeIP/iotname/iotid/r1/r2/r3/r4/localip/current/temperature/humidity
    String DBget = "/" + APssid;
    DBget += "/" + String(MAC_char);
    DBget += "/" + String(R1Status);
    DBget += "/" + String(R2Status);
    DBget += "/" + String(R3Status);
    DBget += "/" + String(R4Status);      
    DBget += "/" + String(mylocalIPString);
    DBget += "/" + String(currentValue);
    DBget += "/" + tempDht;
    DBget += "/" + humDht;
    DBget += "/";
    return DBget;
  }

// -------------------------------------- Networks connection -------------------------------------------
  void sort(int a[], int size, int r[]) {
    for (int i = 0; i < (size - 1); i++) {
      for (int o = 0; o < (size - (i + 1)); o++) {
        if (a[o] < a[o + 1]) {
          int t = a[o];
          a[o] = a[o + 1];
          a[o + 1] = t;
          t = r[o];
          r[o] = r[o + 1];
          r[o + 1] = t;
        }
      }
    } // used to sort the wifi access point list
  }
      
  void WifiToVars() {
    IPAddress myIP;
    IPAddress mylocalIP;
    IPAddress mysubnetMask;
    IPAddress mygatewayIP;

    myIP = WiFi.softAPIP();
    sprintf(myIPString, "%d.%d.%d.%d", myIP[0], myIP[1], myIP[2], myIP[3]);
    mylocalIP = WiFi.localIP();
    sprintf(mylocalIPString, "%d.%d.%d.%d", mylocalIP[0], mylocalIP[1], mylocalIP[2], mylocalIP[3]);
    mysubnetMask = WiFi.subnetMask();
    sprintf(mysubnetMaskString, "%d.%d.%d.%d", mysubnetMask[0], mysubnetMask[1], mysubnetMask[2], mysubnetMask[3]);
    mygatewayIP = WiFi.gatewayIP();
    sprintf(mygatewayIPString, "%d.%d.%d.%d", mygatewayIP[0], mygatewayIP[1], mygatewayIP[2], mygatewayIP[3]); //// turns IP chars to strings
  }

  void APconnect(String ssid, String pwd) {
   WiFi.softAP(ssid.c_str(), pwd.c_str());
   needUpdate=true;
  }

  void STconnect(String ssid, String pwd) {
    Serial.print("Connecting to : ");
    Serial.println(ssid);
    WiFi.begin(ssid.c_str(), pwd.c_str());
    RGB(1);
    int i = 0;
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(WiFi.status());
      i++;
      if (i > 30) {
        Serial.println("");
        Serial.print("Connection failed : ");
        WifiLastStatus = wifistatestring[WiFi.status()];
        Serial.println(WifiLastStatus);
        WifiActive = false;
        WiFi.disconnect();
        WiFi.mode(WIFI_AP);
        RGB(0);
        break;
      }
    }
    Serial.println("");

    if (WiFi.status() == WL_CONNECTED) {
      WifiActive = true;
      Serial.print("Connected to : ");
      Serial.println(ssid);
      Serial.print("Open http://");
      Serial.println(WiFi.localIP());
      RGB(2);
      needUpdate=true;
    }
  }

  void MDNSstart(const char* h, IPAddress IP) {
    if (MDNS.begin(h, IP)) {
      Serial.println("MDNS responder started");
      MDNS.addService("http", "tcp", 80);
      MDNS.addService("ws", "tcp", 81);
      Serial.print("Open http://");
      Serial.print(host);
      Serial.println(".local");
    }
    else {
      Serial.println("MDNS.begin failed");
    }
  }

// ---------------------------------------- Websockets events ----------------------------------------
  void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    Serial.printf("webSocketEvent(%d, %d, ...)\r\n", num, type);
    switch (type) {
      case WStype_DISCONNECTED:
        Serial.printf("[%u] Disconnected!\r\n", num);
        break;
      case WStype_CONNECTED:
        {
          IPAddress ip = webSocket.remoteIP(num);
          Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\r\n", num, ip[0], ip[1], ip[2], ip[3], payload);
          // Send the current Relay status
          if (R1Status) {
            webSocket.sendTXT(num, R1ON, strlen(R1ON));
          }
          else {
            webSocket.sendTXT(num, R1OFF, strlen(R1OFF));
          }
          if (R2Status) {
            webSocket.sendTXT(num, R2ON, strlen(R2ON));
          }
          else {
            webSocket.sendTXT(num, R2OFF, strlen(R2OFF));
          }
          if (R3Status) {
            webSocket.sendTXT(num, R3ON, strlen(R3ON));
          }
          else {
            webSocket.sendTXT(num, R3OFF, strlen(R3OFF));
          }
          if (R4Status) {
            webSocket.sendTXT(num, R4ON, strlen(R4ON));
          }
          else {
            webSocket.sendTXT(num, R4OFF, strlen(R4OFF));
          }
        }
        break;
      case WStype_TEXT:
        Serial.printf("[%u] get Text: %s\r\n", num, payload);

        if ((length == strlen(R1ON)) && (memcmp(R1ON, payload, strlen(R1ON)) == 0)) {
          writeRelay(1,true);
        }
        else if ((length == strlen(R1OFF)) && (memcmp(R1OFF, payload, strlen(R1OFF)) == 0)) {
          writeRelay(1,false);
        }

        if ((length == strlen(R2ON)) && (memcmp(R2ON, payload, strlen(R2ON)) == 0)) {
          writeRelay(2,true);
        }
        else if ((length == strlen(R2OFF)) && (memcmp(R2OFF, payload, strlen(R2OFF)) == 0)) {
          writeRelay(2,false);
        }
        
        if ((length == strlen(R3ON)) && (memcmp(R3ON, payload, strlen(R3ON)) == 0)) {
          writeRelay(3,true);
        }
        else if ((length == strlen(R3OFF)) && (memcmp(R3OFF, payload, strlen(R3OFF)) == 0)) {
          writeRelay(3,false);
        }
        
        if ((length == strlen(R4ON)) && (memcmp(R4ON, payload, strlen(R4ON)) == 0)) {
          writeRelay(4,true);
        }
        else if ((length == strlen(R4OFF)) && (memcmp(R4OFF, payload, strlen(R4OFF)) == 0)) {
          writeRelay(4,false);
        }
        
        // add else if readcurrent for real time printing
        //else {
        //  Serial.println("Unknown command");
        //}


        // send data to all connected clients
        webSocket.broadcastTXT(payload, length);
        break;
      case WStype_BIN: //  not necessary ?
        Serial.printf("[%u] get binary length: %u\r\n", num, length);
        hexdump(payload, length);

        // echo data back to browser
        webSocket.sendBIN(num, payload, length);
        break;
      default:
        Serial.printf("Invalid WStype [%d]\r\n", type);
        break;
    }
  }

// ------------------------------------- Timer interrupt ---------------------------------------------
  void timerUpdate(void *pArg) {
        tickUpdate = true;
  }

  void timerDHT(void *pArg) {
        tickDHT = true;
  }

  void timerCurrent(void *pArg) {
        tickCurrent = true;
  }

// --------------------------------------------- Setup -----------------------------------------------
void setup(void) {
// --------------- Pin Initialisation
  delay(100);
  pinMode(ledRed, OUTPUT);
  pinMode(ledBlue, OUTPUT);
  pinMode(ledGreen, OUTPUT);
  pinMode(R1PIN, OUTPUT);
  pinMode(R2PIN, OUTPUT);
  pinMode(R3PIN, OUTPUT);
  pinMode(R4PIN, OUTPUT);
  pinMode(A0, INPUT);

// --------------- Output Initialisation 
  // (Inverted logic depending on the type of the relays)
  digitalWrite(R1PIN, HIGH);
  digitalWrite(R2PIN, HIGH);
  digitalWrite(R3PIN, HIGH);
  digitalWrite(R4PIN, HIGH);

// --------------- Serial Initialisation
  Serial.begin(115200);
  Serial.print("\n");

// --------------- Files system initialization
  SPIFFS.begin();
  FSInfo info;
  SPIFFS.info(info);

// ---------------- File system statistics
  Serial.printf("Total: %u\nUsed: %u\nFree space: %u\nBlock size: %u\nPage size: %u\nMax open files: %u\nMax path len: %u\n",
                info.totalBytes,
                info.usedBytes,
                info.totalBytes - info.usedBytes,
                info.blockSize,
                info.pageSize,
                info.maxOpenFiles,
                info.maxPathLength
               );
  {
    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) {
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      Serial.printf("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
    }
    Serial.printf("\n");
  }

// --------------- Connection to server and access point start
  readWifiData(InfoFile);
  readHomeData(HomeFile);
  Serial.println(STssid);
  Serial.println(STpwd);
  STconnect(STssid, STpwd);
  APconnect(APssid, APpwd);
  MDNSstart(host, WiFi.localIP());

// ---------------- Web services begin
  server.begin();
  Serial.println("HTTP server started");

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("webSocket service started");

  Serial.print("Module MAC address : ");
  Serial.println(macstr());

  WifiToVars(); // Write the networks IP in their respective variables
  Serial.print("AP IP address: ");
  Serial.println(myIPString);

// ---------------- Server.on definition

// -------- Server.on / File system
  server.onNotFound([]() {  if (!handleFileRead(server.uri()))server.send(404, "text/plain", "FileNotFound");});

  server.on("/list", HTTP_GET, handleFileList);

// -------- Server.on / Standalone mode
  server.on("/aplist", HTTP_GET, []() {  server.send(200, "text/json", wifiscan());});
  server.on("/apstate", HTTP_GET, []() {  server.send(200, "text/json", wifistate());});
  server.on("/wifiparam", HTTP_GET, []() {  server.send(200, "text/json", wifiparam());});

  server.on("/current", HTTP_GET, []() {
  currentRead();
  server.send(200, "text/json", currentjson());
  });

  server.on("/dht", HTTP_GET, []() {
  dhtRead();
  server.send(200, "text/json", dhtjson());
  });

// -------- Server.on / Home server connection
  server.on("/handshake", HTTP_GET, []() {
  boolean change=false;
  if (server.args() != 0) {
    if (server.hasArg("iotname")) {
      if (server.arg("iotname") != APssid) {
        APssid = server.arg("iotname");
        change = true;
      }
    }
    //      if (server.hasArg("pwd" == true and server.arg("pwd")!=APpwd)){APpwd=server.arg("pwd");

    if (server.hasArg("homeip")) {
      if (server.arg("homeip")!=HomeIP) {
      HomeIP = server.arg("homeip");
      change = true;
      }
    }
    if (server.hasArg("homeport")) {
      if (server.arg("homeport")!=String(homeport)) {
      homeport = (server.arg("homeport")).toInt();
      change = true;
      }
    }
    server.send(200, "text/json", iotDBjson());
    if(change){
    writeHomeData(HomeFile);
    WriteWifiData(InfoFile);
    APconnect(APssid, APpwd);
    }
  }
  });

  server.on("/rgb", HTTP_GET, []() {
  if (server.args() != 0) {
    if (server.hasArg("color")) { // add a bug protection if values are not between 0 and 7
          RGB(server.arg("color").toInt());
    }
    server.send(200, "text/json", iotDBjson());
  }
  });

  server.on("/relay", []() { // Ready for multiple relays
  if (server.args() != 0) {
    if (server.hasArg("r1")) {
      if (server.arg("r1") == "on") {
        writeRelay(1,HIGH);
      }
      if (server.arg("r1") == "off") {
        writeRelay(1,LOW);
      }
    }
    if (server.hasArg("r2")) {
      if (server.arg("r2") == "on") {
        writeRelay(2,HIGH);
      }
      if (server.arg("r2") == "off") {
        writeRelay(2,LOW);
      }
    }
    if (server.hasArg("r3")) {
      if (server.arg("r3") == "on") {
        writeRelay(3,HIGH);
      }
      if (server.arg("r3") == "off") {
        writeRelay(3,LOW);
      }
    }
    if (server.hasArg("r4")) {
      if (server.arg("r4") == "on") {
        writeRelay(4,HIGH);
      }
      if (server.arg("r4") == "off") {
        writeRelay(4,LOW);
      }
    }        
  }
  server.send(200, "text/json", iotDBjson()); //do we need to update dht and current before sending iotDBjson ?
  });

  server.on("/iotname", []() {
  if (server.args() != 0 && server.arg("name") != "" ) {
    String newAPname = server.arg("name");
    Serial.print("Changing access point name to ");
    Serial.println(newAPname);
    if (APssid != newAPname) {
      APssid = newAPname;
      WriteWifiData(InfoFile);
      server.send(200, "text/json", "{\"iotname\":\"" + APssid + "\"}");
      WiFi.softAP(newAPname.c_str(), APpwd.c_str());
    }
    server.send(200, "text/json", iotDBjson());
  }
  else {
    server.send(200, "text/json", iotDBjson());
  }
  });

  server.on("/apconnect", []() {
  String ssid = server.arg("ssid");
  String pwd = server.arg("pwd");
  if (ssid != "") {
    if (ssid != STssid || pwd != STpwd) { //futur : if (pwd.length() >8 || pwd == ""){do things}else{"bad password"}
      STssid = ssid;
      STpwd = pwd;
      WriteWifiData(InfoFile);
      server.send(200, "text/json", "{\"apstate\":\"Connecting\",\"apname\":\"" + STssid + "\"}");
      STconnect(STssid, STpwd);
      WifiToVars(); // update wifi IPs
    }
    else {
      if ( WifiActive == true) {
        server.send(200, "text/json", "{\"apstate\":\"" + wifistatestring[WiFi.status()] + "\",\"apname\":\"" + STssid + "\"}");
      }
      else {
        server.send(200, "text/json", "{\"apstate\":\"Not connected\",\"apname\":\"" + STssid + "\"}");
      }
    }
  }
  });

  server.on("/update", HTTP_GET, []() {
  currentRead();
  dhtRead();
  server.send(200, "text/json", iotDBjson());
  });

  server.on("/load", HTTP_GET, []() {
  server.send(200, "text/json", loadjson());
  });

  server.on("/crypto", HTTP_GET, []() {
  server.send(200, "text/json", cryptojson());
  });

// ------------- Timer initialisation
  tickUpdate = false;
  os_timer_setfn(&myTimer1, timerUpdate, NULL);
  os_timer_arm(&myTimer1, 30000, true);

  tickDHT = false;
  os_timer_setfn(&myTimer2, timerDHT, NULL);
  os_timer_arm(&myTimer2, 2000, true);

  tickCurrent = false;
  os_timer_setfn(&myTimer3, timerCurrent, NULL);
  os_timer_arm(&myTimer3, 500, true);

// -------------- First home connection
  currentRead();
  dhtRead();
  sendDataClient();
}

//---------------------------------------------- Loop ------------------------------------------------
void loop(void) {
webSocket.loop();
server.handleClient(); // comment fusionner avec listenClient() ?

// ---------- On timer interrups 
  if (tickUpdate == true) { 
    // si les valeurs ont changé depuis la dernière fois + gestion du dernier envoi de data (doit etre "ok")
      //Serial.println("Tick Update Occurred : 30s");
      tickUpdate = false;
      dhtRead();
      currentRead();

      if(!waitingClient){
          sendDataClient();
      }
      clientTimeout++;
  }

  if (tickDHT == true) { 
          dhtRead();
          //Serial.println("Tick DHT Occurred : 2s");
          tickDHT = false;
   }

  if (tickCurrent == true) { 
          //currentRead();
          //Serial.println("Tick Current Occurred : 0.5s");
          tickCurrent = false;
   }

// ---------- On waiting client
  if(waitingClient){
  listenClient(); // can't be moved before the timer feature
  }

// ---------- On need update
  if(needUpdate){
  sendDataClient(); // can't be moved before the timer feature
  needUpdate =false;
  Serial.println("need update : ok");
  os_timer_arm(&myTimer1, 30000, true);
  }
  
// ---------- On incoming serial message
  if (Serial.available() > 0) {
    String sdata = Serial.readString();
    Serial.println(sdata);
    WriteSerialData("/serial.txt",sdata);
  }

yield(); //understand and test if it is necessary?!
}

//-------------- Home connection handling as a server to client
  void listenClient() { // il serait mieux de le remplacer par un "server.on"? ne fonctionne pas sur numericable
    if (homeclient.available()){
    String line = homeclient.readStringUntil('\r');
    int a= line.indexOf('"data":');
    String response = line.substring(a+2,a+4);
      if (response=="ok"){
      homeClientConnected=true;
      HomeState="Connected";
      //needUpdate=false;
      RGB(3);
      Serial.println("Data received : Home connected");
      waitingClient=false;
      
      }
    homeClientConnected=false;
    //HomeState="Not connected";
    clientTimeout=0;
    }
    if(clientTimeout>3){
    homeClientConnected=false;
    HomeState="Not connected";
    clientTimeout=0;
    waitingClient=false;
    RGB(0);
    
    Serial.println("Client timeout : Home disconnected");
    }
  }

  void sendDataClient() { //Equivalent to "home connect"
    if (homeClientConnected==false &&  WifiActive == true){
          if (!homeclient.connect(HomeIP.c_str(), homeport)) {
          Serial.println("connection failed to home ip as client");
          homeClientConnected =false;
          //HomeState="Not connected";
          RGB(0);
          return;
    }         
    homeClientConnected =true;
    Serial.println(HomeIP);

    Serial.print("Requesting URL: ");
    Serial.println(homeurl);
          
    // This will send the request to the server
    homeclient.print(String("GET ") + homeurl + iotDBnewget() +  " HTTP/1.1\r\n" + "Host: " + HomeIP+":" + homeport + "\r\n" + "Connection: close\r\n\r\n");
    //homeclient.print(String("GET ") + "api/esps" +  " HTTP/1.1\r\n" + "Host: " + HomeIP + "\r\n" + "Connection: close\r\n\r\n");
    
    clientTimeout=0;
    waitingClient=true;
    }
  }
