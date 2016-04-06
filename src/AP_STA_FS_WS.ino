// regarder les schémas de stabilisation du esp12 avec condo!

#include <Arduino.h>
#include <Hash.h>
#include <WebSocketsServer.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <FS.h>

//ajouter id unique pour chaque retour de json

String wifistatestring[7]={"Inactive","No SSID available","Scan completed","Connected","Connection failed","Connection lost","Connecting"};
const char* STssid = "ES_2590";
const char* STpassword = "46600000";
const char* APssid = "Light Switch";
const char* APpassword = "12345678";
const char* host = "switch";
uint8_t MAC_array[6];
char MAC_char[18];
const int R1PIN = 2;
bool R1Status;

// Commands sent through Web Socket
const char R1ON[] = "r1on\n";
const char R1OFF[] = "r1off\n";


ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
//holds the current upload
File fsUploadFile;

//format bytes
String formatBytes(size_t bytes){
  if (bytes < 1024){
    return String(bytes)+"B";
  } else if(bytes < (1024 * 1024)){
    return String(bytes/1024.0)+"KB";
  } else if(bytes < (1024 * 1024 * 1024)){
    return String(bytes/1024.0/1024.0)+"MB";
  } else {
    return String(bytes/1024.0/1024.0/1024.0)+"GB";
  }
}

String getContentType(String filename){
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool handleFileRead(String path){
  Serial.println("handleFileRead: " + path);
  if(path.endsWith("/")) path += "index.htm";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
    if(SPIFFS.exists(pathWithGz))
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
  while(dir.next()){
    File entry = dir.openFile("r");
    if (output != "[") output += ',';
    bool isDir = false;
    output += "{\"type\":\"";
    output += (isDir)?"dir":"file";
    output += "\",\"name\":\"";
    output += String(entry.name()).substring(1);
    output += "\"}";
    entry.close();
  }
  
  output += "]";
  server.send(200, "text/json", output);
}

void writeR1(bool R1on){
  R1Status = R1on;
  // Note inverted logic for Adafruit HUZZAH board
  if (R1on) {
    digitalWrite(R1PIN, LOW);
  }
  else {
    digitalWrite(R1PIN, HIGH);
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length){
  Serial.printf("webSocketEvent(%d, %d, ...)\r\n", num, type);
  switch(type) {
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

void sort(int a[], int size,int r[]) {
    for(int i=0; i<(size-1); i++) {
        for(int o=0; o<(size-(i+1)); o++) {
                if(a[o] < a[o+1]) {
                    int t = a[o];
                    a[o] = a[o+1];
                    a[o+1] = t;
                    t = r[o];
                    r[o] = r[o+1];
                    r[o+1] = t;
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
    memset(rssi,0,sizeof(rssi));
    for (int i = 0; i < n; ++i){
    rssi[i]=WiFi.RSSI(i);
    }
    
    int rank[30];
    for (int i = 0; i < 30; ++i){
      rank[i]=i;
    }

    sort(rssi,n,rank);


    // Print SSID and RSSI in a json for each network found
    String json = "[";
    
    for (int i = 0; i < n; ++i){
    json += "{\"SSID"+String(i)+"\":\""+String(WiFi.SSID(rank[i]))+"\"";
    json += ",\"RSSI"+String(i)+"\":\""+String(WiFi.RSSI(rank[i]))+"\"},";
    }
    json = json.substring(0,json.length()-1);
    json += "]";
    return json;
  }
  //Serial.println("");
}

String wifistate(){
  String json = "[{\"Wifi access point state\":\""+wifistatestring[WiFi.status()]+"\"}]";
  return json;
  }

String macaddress() {
    String mac;
    //std::string mac=MAC_char;
    mac= String(MAC_char);
    String json = "[";
    json += "{Mac address : "  +mac+  "}]";
    return json;
}

void STconnect(){
   Serial.println("");
  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin(host, WiFi.localIP())) {
    Serial.println("MDNS responder started");
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("ws", "tcp", 81);
  }
  else {
    Serial.println("MDNS.begin failed");
  }

  Serial.print("Open http://");
  Serial.print(host);
  Serial.print(".local or http://");
  Serial.println(WiFi.localIP());
  
  }



void setup(void){

  delay(100);
  pinMode(R1PIN, OUTPUT);
  digitalWrite(R1PIN, HIGH);
  Serial.begin(115200);
  Serial.print("\n");
  //Serial.setDebugOutput(true);
  SPIFFS.begin();
  FSInfo info;
  SPIFFS.info(info);


    Serial.printf("Total: %u\nUsed: %u\nFree space: %u\nBlock size: %u\nPage size: %u\nMax open files: %u\nMax path len: %u\n",
                  info.totalBytes,
                  info.usedBytes,
                  info.totalBytes-info.usedBytes,
                  info.blockSize,
                  info.pageSize,
                  info.maxOpenFiles,
                  info.maxPathLength
                 );

  //ajouter espace utilisé/ espace libre
  {
    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) {    
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      Serial.printf("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
    }
    Serial.printf("\n");
  }

  //WiFi.mode(WIFI_AP);
  WiFi.mode(WIFI_AP_STA);
  
  //WIFI AP INIT
  WiFi.softAP(APssid, APpassword);
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);


  //WIFI INIT
  Serial.printf("Connecting to %s\n", STssid);
  if (String(WiFi.SSID()) != String(STssid)) {
    WiFi.begin(STssid, STpassword);
  }
  


  
//is it necessary to use array and char features? String(texte) can work?

  WiFi.macAddress(MAC_array);
    for (int i = 0; i < sizeof(MAC_array); ++i){
      sprintf(MAC_char,"%s%02x:",MAC_char,MAC_array[i]);}
    MAC_char[17]=(char)0;
  Serial.print("ST MAC address : ");
  Serial.println(MAC_char);
  

  server.on("/list", HTTP_GET, handleFileList);

  server.onNotFound([](){if(!handleFileRead(server.uri()))server.send(404, "text/plain", "FileNotFound"); });

  
  server.on("/aplist", HTTP_GET, [](){server.send(200, "text/json", wifiscan());});
  server.on("/apstate", HTTP_GET, [](){server.send(200, "text/json", wifistate());});
  server.on("/iotmac", HTTP_GET, [](){server.send(200, "text/json", macaddress());});

    
  server.begin();
  Serial.println("HTTP server started");
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("webSocket service started");

}
 
void loop(void){
  webSocket.loop();
  server.handleClient();
}

