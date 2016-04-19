// regarder les schémas de stabilisation du esp12 avec condo!

#include <Arduino.h>
#include <Hash.h>
#include <WebSocketsServer.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#include <dht11.h>
#include "user_interface.h" //for timer purpose


dht11 DHT;
#define DHT11_PIN 4

//ajouter id unique pour chaque retour de json ?

String wifistatestring[7] = {"Inactive", "No SSID available", "Scan completed", "Connected", "Connection failed", "Connection lost", "Connecting"};
const char* initSTssid = "ES_2590";
const char* initSTpwd = "46600000";
const char* initAPssid = "Light Switch";
const char* initAPpwd = "12345678";
const char* homeip = "192.168.43.1";
String APssid;
String APpwd;
String STssid;
String STpwd;
String WifiLastStatus;
String InfoFile = "/info.txt";
String HomeFile = "/homeip.txt";
String HomeIP;
boolean WifiActive = false;
boolean homeClientConnected = false;
os_timer_t myTimer;
bool tickOccured;

const int ledRed = 14;      // the number of the LED pins
const int ledBlue = 13;
const int ledGreen = 12;
// color = [black,red,green,blue,cyan,purple,yellow]
const int color[7][3]={{0,0,0},{500,0,0},{0,500,0},{0,0,500},{0,500,500},{300,0,500},{300,500,0}};


const char* host = "switch";
uint8_t MAC_array[6];
char MAC_char[18];
const int R1PIN = 2;
bool R1Status;
char myIPString[24];
char mylocalIPString[24];
char mysubnetMaskString[24];
char mygatewayIPString[24];
int chk; // State of the DHT11 sensor (values are more reliable when chk is declared outside the voids)
int clientTimeout=0;

WiFiClient homeclient;
String homeurl = "index.php"; //  ?espid=mac"; ajouter l'équivalent du dataIotJson sous forme d'arguments du GET

// Commands sent through Web Socket
const char R1ON[] = "r1on\n";
const char R1OFF[] = "r1off\n";


ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
//holds the current upload
File fsUploadFile;

//format bytes
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


void RGB(int colorNum){
 analogWrite(ledRed,color[colorNum][0]);
 analogWrite(ledGreen,color[colorNum][1]);
 analogWrite(ledBlue,color[colorNum][2]);
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

void writeR1(bool Rstate) {  //void writeRelay(bool Rstate,int RelayNumber) for multiple relays
  R1Status = Rstate;
  // Note inverted logic depending on relay type
  if (Rstate) {
    digitalWrite(R1PIN, LOW);
  }
  else {
    digitalWrite(R1PIN, HIGH);
  }
}

int CurrentRead() {

  int temp = 0;
  int maxAmp = 0;

  for (int i = 1; i < 300; i++) {
    temp = analogRead(A0);
    if (temp > maxAmp) {
      maxAmp = temp;
    }
    delayMicroseconds(100);
  }

  //return (0.02354*maxAmp);
  return (maxAmp);
}

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
      }
      break;
    case WStype_TEXT:
      Serial.printf("[%u] get Text: %s\r\n", num, payload);

      if ((length == strlen(R1ON)) && (memcmp(R1ON, payload, strlen(R1ON)) == 0)) {
        writeR1(true);
      }
      else if ((length == strlen(R1OFF)) && (memcmp(R1OFF, payload, strlen(R1OFF)) == 0)) {
        writeR1(false);
      }
      // add else if readcurrent
      else {
        Serial.println("Unknown command");
      }


      // send data to all connected clients
      webSocket.broadcastTXT(payload, length);
      break;
    case WStype_BIN:
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
  }
}

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
    String json = "[";

    for (int i = 0; i < n; ++i) {
      json += "{\"SSID" + String(i) + "\":\"" + String(WiFi.SSID(rank[i])) + "\"";
      json += ",\"RSSI" + String(i) + "\":\"" + String(WiFi.RSSI(rank[i])) + "\"},";
    }
    json = json.substring(0, json.length() - 1);
    json += "]";
    return json;
  }
  //Serial.println("");
}

String wifistate() {
  // add ssid name (if wifistatus=connecting or connected or not exist?)
  String json = "[{\"apstate\":\"" + wifistatestring[WiFi.status()] + "\"}]";
  return json;
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
  sprintf(mygatewayIPString, "%d.%d.%d.%d", mygatewayIP[0], mygatewayIP[1], mygatewayIP[2], mygatewayIP[3]);
}


String wifiparam() {

  WifiToVars();

  String json = "[{\"APip\":\"" + String(myIPString) + "\"";
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
  json += ",\"mac\":\""  + String(MAC_char) +  "\"}]";

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
  String json = "[";
  json += "[{\"Mac address\":\"" + String(MAC_char) +  "\"}]";
  return json;
}

String iotDBjson() {
  String json = "{\"iotname\":\"" + APssid + "\",";
  json += "\"espid\":\"" + String(MAC_char) + "\",";
  json += "\"relay\":\"" + String(R1Status) + "\",";
  json += "\"localip\":\"" + String(mylocalIPString) + "\",";
  json += "\"current\":\"" + String(CurrentRead()) + "\"}";
  return json;
}

String iotDBget() {
  String DBget = "?iotname=" + APssid;
  DBget += "&espid=" + String(MAC_char);
  DBget += "&relay=" + String(R1Status);
  DBget += "&localip=" + String(mylocalIPString);
  DBget += "&current=" + String(CurrentRead());
  return DBget;
}

void APconnect(String ssid, String pwd) {
  WiFi.softAP(ssid.c_str(), pwd.c_str());
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
    if (i > 20) {
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

void ReadWifiData(String filename) {
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

  if (APssid.length() < 1) {
    APssid = initAPssid;
    APpwd = initAPpwd;
  }

  if (STssid.length() < 1) {
    STssid = initSTssid;
    STpwd = initSTpwd;
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

void WriteHomeData(String filename) {
  File f = SPIFFS.open(filename, "w");
  if (!f) {
    Serial.println("file open failed");
  }
  else {
    f.println(HomeIP);
    f.close();
    Serial.println("file writed");
  }
}

void timerCallback(void *pArg) {
      tickOccured = true;
}

void setup(void) {

  delay(100);
  pinMode(ledRed, OUTPUT);
  pinMode(ledBlue, OUTPUT);
  pinMode(ledGreen, OUTPUT);
  pinMode(R1PIN, OUTPUT);
  digitalWrite(R1PIN, HIGH);
  
  pinMode(A0, INPUT);
  Serial.begin(115200);
  Serial.print("\n");




  //Serial.setDebugOutput(true);
  SPIFFS.begin();
  FSInfo info;
  SPIFFS.info(info);


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

  ReadWifiData(InfoFile);
  STconnect(STssid, STpwd);
  APconnect(APssid, APpwd);
  MDNSstart(host, WiFi.localIP());

  server.begin();
  Serial.println("HTTP server started");

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("webSocket service started");

  Serial.print("Module MAC address : ");
  Serial.println(macstr());

  WifiToVars(); // Write the networks IP in the respective variables
  Serial.print("AP IP address: ");
  Serial.println(myIPString);

  // inclure le "All Inclusive json" dans toutes les réponses!

  server.onNotFound([]() {
    if (!handleFileRead(server.uri()))server.send(404, "text/plain", "FileNotFound");
  });
  server.on("/list", HTTP_GET, handleFileList);
  server.on("/aplist", HTTP_GET, []() {
    server.send(200, "text/json", wifiscan());
  });
  server.on("/apstate", HTTP_GET, []() {
    server.send(200, "text/json", wifistate());
  });
  server.on("/iotmac", HTTP_GET, []() {
    server.send(200, "text/json", iotDBjson());
  });
  server.on("/wifiparam", HTTP_GET, []() {
    server.send(200, "text/json", wifiparam());
  });
  server.on("/analog", HTTP_GET, []() {
    server.send(200, "text/json", iotDBjson());
  });
  server.on("/rgb", HTTP_GET, []() {
    if (server.args() != 0) {
      if (server.hasArg("color")) { // add a bug protection if values are not between 0 and 7
            RGB(server.arg("color").toInt());
      }
      server.send(200, "text/json", iotDBjson());
    }
  });
  
  server.on("/dht", HTTP_GET, []() {
    chk = DHT.read(DHT11_PIN);    // READ DATA
    switch (chk) {
      case DHTLIB_OK:
        Serial.print("OK,\t");
        server.send(200, "text/json", "{\"Temp\":\"" + String(DHT.temperature) + ",\"Hum\":\"" + String(DHT.humidity) + "\"}");
        break;
      case DHTLIB_ERROR_CHECKSUM:
        Serial.print("Checksum error,\t");
        server.send(200, "text/json", "{\"Temp\":\"error\",\"Hum\":\"error\"}");
        break;
      case DHTLIB_ERROR_TIMEOUT:
        Serial.print("Time out error,\t");
        server.send(200, "text/json", "{\"Temp\":\"error\",\"Hum\":\"error\"}");
        break;
      default:
        Serial.print("Unknown error,\t");
        server.send(200, "text/json", "{\"Temp\":\"error\",\"Hum\":\"error\"}");
        break;
    }
  });

  server.on("/handshake", HTTP_GET, []() {
    if (server.args() != 0) {
      if (server.hasArg("iotname")) {
        if (server.arg("iotname") != APssid) {
          APssid = server.arg("iotname");
        }
      }
      //      if (server.hasArg("pwd" == true and server.arg("pwd")!=APpwd)){APpwd=server.arg("pwd");

      if (server.hasArg("homeip")) {
        HomeIP = server.arg("homeip");
        WriteHomeData(HomeFile);
        WriteWifiData(InfoFile);
      }


      server.send(200, "text/json", iotDBjson());
      APconnect(APssid, APpwd);
    }
  });

  server.on("/relay", []() { // Ready for multiple relays
    if (server.args() != 0) {
      if (server.hasArg("r1")) {
        if (server.arg("r1") == "on") {
          writeR1(HIGH);
        }
        if (server.arg("r1") == "off") {
          writeR1(LOW);
        }
      }
    }
    server.send(200, "text/json", iotDBjson());
  });

  server.on("/iotname", []() {
    if (server.args() != 0 && server.arg("name") != "" ) {
      String newAPname = server.arg("name");
      Serial.print("Changing access point name to ");
      Serial.println(newAPname);
      if (APssid != newAPname) {
        APssid = newAPname;
        WriteWifiData(InfoFile);
        server.send(200, "text/json", "[{\"iotname\":\"" + APssid + "\"}]");
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
        server.send(200, "text/json", "[{\"apstate\":\"Connecting\"},{\"apname\":\"" + STssid + "\"}]");
        STconnect(STssid, STpwd);
        WifiToVars(); // update wifi IPs
      }
      else {
        if ( WifiActive == true) {
          server.send(200, "text/json", "[{\"apstate\":\"" + wifistatestring[WiFi.status()] + "\"},{\"apname\":\"" + STssid + "\"}]");
        }
        else {
          server.send(200, "text/json", "[{\"apstate\":\"Not connected\"},{\"apname\":\"" + STssid + "\"}]");
        }
      }
    }
    else {
      if ( WifiActive == true) {
        server.send(200, "text/json", "[{\"apstate\":\"" + wifistatestring[WiFi.status()] + "\"},{\"apname\":\"" + STssid + "\"}]");
      }
      else {
        server.send(200, "text/json", "[{\"apstate\":\"Not connected\"},{\"apname\":\"" + STssid + "\"}]");
      }
    }

  });
  // Connection to Home      -- Keep alive issues ? ------------------------------------------ to do -------------------------------------------
  
// if homeip!="" (in a home.txt file )
// sendDataClient()
// wait 5s for response
// if data=sent then blueled et home=connecté

  //Timer init
  tickOccured = false;
  os_timer_setfn(&myTimer, timerCallback, NULL);
  os_timer_arm(&myTimer, 5000, true);

}

void loop(void) {
  webSocket.loop();
  server.handleClient();

    if (tickOccured == true) {
      // si les valeurs ont changé depuis la dernière fois + gestion du dernier envoi de data (doit etre "ok")
        Serial.println("Tick Occurred");
        tickOccured = false;
        sendDataClient();
        clientTimeout++;
     }
     
   listenClient();


yield(); //humhum?!
}

void listenClient(){
    if (homeclient.available()){
    String line = homeclient.readStringUntil('\r');
    int a= line.indexOf('"data":');
    String response = line.substring(a+2,a+4);
      if (response=="ok"){
      Serial.println("Data received");
      }
    homeClientConnected=false;
    clientTimeout=0;
    }
    if(clientTimeout>3){
    homeClientConnected=false;
    clientTimeout=0;
    Serial.println("Client timeout");
    }
}

void sendDataClient(){
    if (homeClientConnected==false &&  WifiActive == true){
            if (!homeclient.connect(homeip, 8080)) { 
            Serial.println("connection failed to home ip as client");
            return;
            }
    homeClientConnected =true;    
    Serial.print("Requesting URL: ");
    Serial.println(homeurl);
          
    // This will send the request to the server
    homeclient.print(String("GET ") + homeurl + iotDBget() +  " HTTP/1.1\r\n" +
                 "Host: " + homeip + "\r\n" + 
                 "Connection: close\r\n\r\n");
   clientTimeout=0;
    }
}
