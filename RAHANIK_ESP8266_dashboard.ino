IM#include <NTPClient.h>
#include <EEPROM.h>
#include "FS.h"
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>

#define frm_version "1" // Number of version that must be a string value

#define MAX_PIN_COUNT 17
#define MAX_PIN_NAME 20

typedef struct
{
    char name[MAX_PIN_NAME];
    int value;
    bool isInUse;
}Pin;

Pin pins[MAX_PIN_COUNT];

#define LED_WIFI 12

const String BACKUP_CONF_FILENAME = "Conf.bkp";
const String CONF_FILE = "/conf.txt";
unsigned long previousMillis[2]; //[x] = number of leds  its used in Flashing Functions

File UploadFile;

unsigned long flag_update_click = 0;
const String UPDATE_SERVER = "YOUR UPDATE LINK";
String filename;

String msgForWifiSetting = ""; // a message that shown in wifi page


ESP8266WebServer WEB_SERVER(80);
//DNSServer DNS_SERVER;
HTTPClient http;
WiFiServer Wifiserver(80);  // open port 80 for server connection

boolean WiFi_Status = false; // F = Active WiFi LED , T = Deactive WiFi LED
uint8_t wifi_connection_times = 1; // Times of try connecting to the WiFi
unsigned long wifi_connection_time = 0; // time to try connecting to the WiFi

const IPAddress AP_IP(192, 168, 2, 1);

String AP_SSID;
String AP_PASS = "123456789";
String STA_RSSI;
boolean SETUP_MODE;
String SSID_LIST;

unsigned long Begin_Connection_Time = 0;
boolean Connected = false;  // Connection Flag
boolean Start_AP = false; // Version 39 / signe to Start AP at begining
boolean Try_Connecting = true;  // Version 39 // in setup_mode() if save new WiFi setting dont let establish connection
String ssid2 = "SSID";
String password = "SSID_PASSWORD";
String LOCAL_IP;

uint8_t temp_ip[4]; // Temporary place to convert String to IP format
boolean dhcp = true; //ssid DHCP  used in conf.txt

//Static IP address configuration
IPAddress staticIP(192, 168, 1, 100); //Nikban static ip
IPAddress gateway(192, 168, 1, 1); //IP Address of your WiFi Router (Gateway)
IPAddress subnet(255, 255, 255, 0); //Subnet mask
IPAddress dns(8, 8, 8, 8); //DNS
IPAddress dns2(8, 8, 8, 8); //DNS2


WiFiEventHandler gotIpEventHandler, disconnectedEventHandler;

void bootstrap()
{
  if (!SPIFFS.exists("/bootstrap.min.css.gz"))
  {
    Serial.println(F("/bootstrap.min.css.gz it doesn`t exist"));
  } else {
    File file = SPIFFS.open("/bootstrap.min.css.gz", "r");
    WEB_SERVER.sendHeader("Expires", "Mon, 1 Jan 2222 10:10:10 GMT");
    size_t sent = WEB_SERVER.streamFile(file, "text/css");
    //Serial.println(F("/bootstrap.min.css.gz"));
  }
}

void mycss()
{ //SPIFFS.remove("/mycss.css.gz");
  if (!SPIFFS.exists("/mycss.css.gz"))
  {
    //Serial.println(F("/mycss.css.gz it doesn`t exist"));
  } else {
    File file = SPIFFS.open("/mycss.css.gz", "r");
    WEB_SERVER.sendHeader("Expires", "Mon, 1 Jan 2222 10:10:10 GMT");
    size_t sent = WEB_SERVER.streamFile(file, "text/css");
    //Serial.println(F("/mycss.css.gz"));
  }
}

void setup() {
  pinMode(LED_WIFI, OUTPUT);
  digitalWrite(LED_WIFI, 0);
  Serial.begin(9600);
  EEPROM.begin(73);
  SPIFFS.begin();
  
  Init();
  Serial.println("Verison " + (String)frm_version);
  AP_SSID = F("Rahanik_");
  AP_SSID.concat(String(ESP.getChipId()));
  WiFi.persistent(false);
  //WiFi.setPhyMode(WIFI_PHY_MODE_11G);
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.hostname(AP_SSID);
  http.setReuse(true);
  
  //wipe_eeprom();
  //SPIFFS.remove(CONF_FILE+"2");
  /*
    Serial.println("Please wait 30 secs for SPIFFS to be formatted");
    SPIFFS.format();
    Serial.println("Spiffs formatted");
  */

  if (loadSavedConfig()) {
    if (checkWiFiConnection()) {
      Begin_Connection_Time = millis();
      Start_AP = false;
      //IPAddress ip = WiFi.localIP();
      //LOCAL_IP = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
      delay(250);
      WiFi.printDiag(Serial); 
      getRSSI();
      Serial.println("RSSI: " + STA_RSSI + " dBm");  // Version 37
    } else {
      Begin_Connection_Time = 0;
      StartAccessPoint();
    }
  }

  WEB_SERVER.on("/", dashboard);
  WEB_SERVER.on("/reboot", reboot);
  WEB_SERVER.on("/update", update_online);
  WEB_SERVER.on("/restore", restore_page);
  WEB_SERVER.on("/wifiSetting", wifiSetting);
  WEB_SERVER.on("/wifiSettingAction", wifiSettingAction);
  WEB_SERVER.on("/dashboard", dashboard);
  WEB_SERVER.on("/list", HTTP_GET, handleFileList);


    WEB_SERVER.on("/addPin", HTTP_POST, onAddPin);
    //Rota para remover um pino da lista
    WEB_SERVER.on("/removePin", HTTP_POST, onRemovePin);
    //Rota mudar o estado de um pino
    WEB_SERVER.on("/digitalWrite", HTTP_POST, onDigitalWrite);
    //Rota para retornar a lista de pinos em uso, com nome e valor do estado atual
    WEB_SERVER.on("/pinList", HTTP_GET, onPinList);
    //Quando fizerem requisição em uma rota não declarada
    WEB_SERVER.onNotFound(onNotFound);
    
    //WEB_SERVER.serveStatic("/", SPIFFS, "/automacao.html");
    //loadConfig();
  


  // use for any restore action - just by comparing file name
  WEB_SERVER.onFileUpload([]() {
    //File UploadFile;
    if (WEB_SERVER.uri() == "/restoreAction") {
      HTTPUpload& upload = WEB_SERVER.upload();
      if (upload.status == UPLOAD_FILE_START) {
        filename = upload.filename;
        //Serial.print("Upload Name: "); Serial.println(filename);
        UploadFile = SPIFFS.open("/" + filename, "w");
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (UploadFile) {
          UploadFile.write(upload.buf, upload.currentSize);
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        //Serial.println("UploadFile Size " + String(upload.currentSize));
        if (UploadFile)
          UploadFile.close();
          if (SPIFFS.exists("/" + filename)) {
            Serial.println(filename + " is uploaded");
          }
      }
    } else {
      return;
    }
  });

  WEB_SERVER.on("/restoreAction", HTTP_POST, []() {
    WEB_SERVER.sendHeader("Connection", "close");
    WEB_SERVER.sendHeader("Access-Control-Allow-Origin", "*");
    WEB_SERVER.send(200, "text/html", (Update.hasError()) ? makePage("Upload File", "<div class=\"mycontentbody\"><h4 class=\"text-danger\">Uploading failed</h4></div>", true) : makePage("Upload File", "<div class=\"mycontentbody\"><h4 class=\"text-success\">Upload successfully.</h4></div>", true));
    //ESP.restart();
  });

  WEB_SERVER.on("/bootstrap.min.css", bootstrap);
  WEB_SERVER.on("bootstrap.min.css", bootstrap);
  WEB_SERVER.on("/mycss.css", mycss);
  WEB_SERVER.on("mycss.css", mycss);

  const char * headerkeys[] = {"User-Agent", "Cookie"} ;
  size_t headerkeyssize = sizeof(headerkeys) / sizeof(char*);
  //ask server to track these headers
  WEB_SERVER.collectHeaders(headerkeys, headerkeyssize);
  WEB_SERVER.begin();
  
  Serial.println("=========== Rahanik is Ready to work ===========");

  gotIpEventHandler = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP& event)
  {
    Serial.print("Station connected, IP: ");
    Serial.println(WiFi.localIP());
  });
}

void loop() {  
  if (WiFi.status() != WL_CONNECTED) {
    Connected = false;
    LOCAL_IP = "x.x.x.x";
    Begin_Connection_Time = 0;
    if(WiFi.getMode() != WIFI_AP){
      digitalWrite(LED_WIFI, LOW);
    }else{
      if (!WiFi_Status){Flash_LED(LED_WIFI, 100, 600, 0);} //Flash_LED(LED,tOn, tOff,One of the previousMillis);
    }
    if(Start_AP & Try_Connecting){setupMode();} 
  } else if (WiFi.status() == WL_CONNECTED) {
      Connected = true;
      Start_AP = false; // Version 39 // Dont let Access Point Start after first succesfully connection    
      if (!WiFi_Status){Flash_LED(LED_WIFI, 70, 2000, 0);} //Flash_LED(LED,tOn, tOff,One of the previousMillis);    
      if ( LOCAL_IP == "x.x.x.x") {
        IPAddress ip = WiFi.localIP();
        LOCAL_IP = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);  
      }
      if (Begin_Connection_Time == 0) {
        Begin_Connection_Time = millis();
      }
    }    
  WEB_SERVER.handleClient();
}
  

///////////////////////////////
//Checking Login Flag
boolean checkLoginFlag() {
  return false;
}


void handleFileList()
{
  String path = "/";
  // Assuming there are no subdirectories
  Dir dir = SPIFFS.openDir(path);
  String output = "[";
  while (dir.next())
  {
    File entry = dir.openFile("r");
    // Separate by comma if there are multiple files
    if (output != "[")
      output += ",";
    output += String(entry.name()).substring(1);
    entry.close();
  }
  output += "]";
  WEB_SERVER.send(200, "text/plain", output);
}


void restore_page() {
  String s = F("<div class=\"panel-group\"> <div class=\"panel panel-primary\"> <div class=\"panel-heading panel-title\">Select your file  </div> <div class=\"panel-body\"> <form class=\"form-inline\" method=\"post\" action=\"/restoreAction\" enctype=\"multipart/form-data\"> <div class=\"form-group\"> <input class=\"btn btn-default\" id=\"chs\" type=\"file\" name=\"update\"> <br> <button class=\"btn btn-primary btn-block\" type=\"submit\">Upload</button> </div> </form> </div> </div> </div>");
  WEB_SERVER.send(200, "text/html", makePage("Upload File in SPIFFS", s, true));
}

void fetchSetting() { // Get all settings from conf.txt
  String key;
  String value;
  Serial.println(F("Reading Conf.txt"));
  File f;
  f = SPIFFS.open(CONF_FILE, "r");
  while (f.position() < f.size())
  {
    String s;
    s = f.readStringUntil('\n');
    s.trim();
    Serial.println(s);
    if (s.length() != 0) {
      int SeperateIdx = s.indexOf(',', 0);
      key = s.substring(0, SeperateIdx);
      value = s.substring(SeperateIdx + 1);
      Serial.println("Key: " + key + " ,Value: " + value);
      if (key == "WSL") { //Deactive WiFi Status LED
        if (value == "1") {
          WiFi_Status = true;
        } else {
          WiFi_Status = false;
        }
      }
      if (key == "ssid") { // ssid
        if (value != "") {
          ssid2 = value;
        } // else Default value
      }
      if (key == "ssidpass") { // ssidpass
        if (value != "") {
          password = value;
        } // else Default value
      }
      if (key == "IP") { //IP Address
        if (value != "") {
          StringToIp(value);
          staticIP = IPAddress(temp_ip[0], temp_ip[1], temp_ip[2], temp_ip[3]);
        }
      }
      if (key == "GTW") { //Gateway
        if (value != "") {
          StringToIp(value);
          gateway = IPAddress(temp_ip[0], temp_ip[1], temp_ip[2], temp_ip[3]);
        }
      }
      if (key == "SBNT") { //Subnet Mask
        if (value != "") {
          StringToIp(value);
          subnet = IPAddress(temp_ip[0], temp_ip[1], temp_ip[2], temp_ip[3]);
        }
      }
      if (key == "DNS") { //DNS
        if (value != "") {
          StringToIp(value);
          dns = IPAddress(temp_ip[0], temp_ip[1], temp_ip[2], temp_ip[3]);
        }
      }
      if (key == "DNS2") { //DNS2
        if (value != "") {
          StringToIp(value);
          dns2 = IPAddress(temp_ip[0], temp_ip[1], temp_ip[2], temp_ip[3]);
        }
      }
      if (key == "DHCP") { //DHCP
        if (value == "1") {
          dhcp = true;
        } else {
          dhcp = false;
        }
      }
    }
  }
  ////Serial.print(F("-----------------------CONF_FILE:"));
  ////Serial.println(f.size());
  f.close();
}


boolean checkArg(String argname) { // Check argumant
  if (WEB_SERVER.hasArg(argname)) {
    return true;
  }
  return false;
}

void saveSettingInline(){ // Save Options in runing Mode
   if (checkLoginFlag()) {
    delay(20);
    return;
  }
  writeInTempconf();  // Write all settings in tempconf.txt
    if (SPIFFS.rename(CONF_FILE, CONF_FILE + "3")) {
      if (SPIFFS.rename("/tempconf.txt", CONF_FILE)) {
        SPIFFS.remove(CONF_FILE + "3");
      } else {
        SPIFFS.remove("/tempconf.txt");
        fetchSetting();
      }
    } else {
      SPIFFS.remove(CONF_FILE + "3");
      fetchSetting();
    }
}

void wifiSetting() {
  if (checkLoginFlag()) {
    delay(50);
    return;
  }
  ScanWiFi();
  fetchSetting();
  delay(20);
  WEB_SERVER.send(200, "text/html", makePage("Wi-Fi Setting", makeWifiPage(), true));
}

void wifiSettingAction() { // Save all settings in conf.txt 2
   if (checkLoginFlag()) {
    delay(20);
    return;
  }
  String input;
  if (checkArg("submit")) { //come from /setting
    if (checkArg("ssid")) { // get SSID
      input = urlDecode(WEB_SERVER.arg("ssid"));
      ssid2 = input;
    }
    if (checkArg("pass")) { // get SSID Password
      input = urlDecode(WEB_SERVER.arg("pass"));
      password = input;
    }
    if (checkArg("dhcp")) { // get DHCP
      input = urlDecode(WEB_SERVER.arg("dhcp"));
      if (input == "on") {
        dhcp = true;
      }
    } else {
      dhcp = false;
    }
    if (checkArg("WSL")) { // get WSL
      input = urlDecode(WEB_SERVER.arg("WSL"));
      if (input == "on") {
        WiFi_Status = true;
      }
    } else {
      WiFi_Status = false;
    }
    if (checkArg("ip")) { // get IP
      input = urlDecode(WEB_SERVER.arg("ip"));
      StringToIp(input);
      staticIP = IPAddress(temp_ip[0], temp_ip[1], temp_ip[2], temp_ip[3]);
    }
    if (checkArg("gtw")) { // get Gateway
      input = urlDecode(WEB_SERVER.arg("gtw"));
      StringToIp(input);
      gateway = IPAddress(temp_ip[0], temp_ip[1], temp_ip[2], temp_ip[3]);
    }
    if (checkArg("sbnt")) { // get Subnet
      input = urlDecode(WEB_SERVER.arg("sbnt"));
      StringToIp(input);
      subnet = IPAddress(temp_ip[0], temp_ip[1], temp_ip[2], temp_ip[3]);
    }
    if (checkArg("dns")) { // get DNS
      input = urlDecode(WEB_SERVER.arg("dns"));
      StringToIp(input);
      dns = IPAddress(temp_ip[0], temp_ip[1], temp_ip[2], temp_ip[3]);
    }
    if (checkArg("dns2")) { // get DNS2
      input = urlDecode(WEB_SERVER.arg("dns2"));
      StringToIp(input);
      dns2 = IPAddress(temp_ip[0], temp_ip[1], temp_ip[2], temp_ip[3]);
    }

    writeInTempconf();
    
    if(!SPIFFS.exists(CONF_FILE)){
      File f4;
      f4 = SPIFFS.open(CONF_FILE, "w");
      f4.println("1");
      f4.close();
    }
    
    if (SPIFFS.rename(CONF_FILE, CONF_FILE + "3")) {
      if (SPIFFS.rename("/tempconf.txt", CONF_FILE)) {
        SPIFFS.remove(CONF_FILE + "3");
        if(Start_AP){ LOCAL_IP = "x.x.x.x"; }
          Serial.println("===> In wifiSettingAction - Start_AP = False");
          Serial.println(F("Start_AP = FALSE"));
          msgForWifiSetting = "<div class=\"mycontentbody\"><h4 class=\"text-success\">Changes Saved</h4><P>Restart the device to apply the new settings.</p></div>";
          wifiSetting();
          Try_Connecting = false; 
          msgForWifiSetting = "";
          return;          
      } else {
        fetchSetting();
        delay(30);
        msgForWifiSetting = "<div class=\"mycontentbody\"><h4 class=\"text-success\">Settings saved failed</h4></div>";
        wifiSetting();
        msgForWifiSetting = "";
      }
    } else {
      fetchSetting();
      delay(30);
      msgForWifiSetting = "<div class=\"mycontentbody\"><h4 class=\"text-success\">Settings saved failed</h4></div>";
      wifiSetting();
      msgForWifiSetting = "";
    }
  } else {
    // Save setting faild
    wifiSetting();
    return;
  }

}

void dashboard() {
  if (checkLoginFlag()) {
    delay(50);
    return;
  }
  WEB_SERVER.send(200, "text/html", makePage("Dashboard", dashboardMaker(), true));
}


/////////////////////
// HTML Generator //
///////////////////
String makeMenu() {
  String s = F("<select class=\"form-control\" id=\"redirectSelect\"> <option value=\"#\" class=\"opmenu\">Menu</option> <option value=\"/dashboard\" class=\"opbold\">Dashboard</option> <option value=\"/wifiSetting\">Wi-Fi Settings</option> <option value=\"/restore\">Upload</option></select>");
  return s;
}


String dashboardMaker() {
  return "Dashboard...";
}

String makeWifiPage() {
  //Serial.println("SSID2: " + String(ssid2));
  String s = msgForWifiSetting;
  s += F("<div class=\" col-sm-offset-1\"><form class=\"form-horizontal\" method=\"post\" action=\"/wifiSettingAction\"><div class=\"form-group\"><label class=\"control-label col-sm-2\" for=\"ssidselect\">SSID:</label><div class=\"col-sm-4\"><select name=\"ssidselect\" id=\"ssidselect\" onChange=\"funsel()\" onClick=\"funsel()\">");
  s += SSID_LIST;
  s += F("</select></div></div><div class=\"form-group\"><label class=\"control-label col-sm-2\" for=\"ssid\">Custom SSID:</label><div class=\"col-sm-4\"><input  name=\"ssid\" type=\"text\" class=\"form-control\" id=\"ssid\" required maxlength=\"32\" value=\"");
  s += ssid2;
  s += F("\"></div></div><div class=\"form-group\">"
         "<label class=\"control-label col-sm-2\" for=\"pass\">Password:</label><div class=\"col-sm-4\"><input name=\"pass\" type=\"password\" class=\"form-control\" id=\"pass\" maxlength=\"32\" value=\"");
  s += String(password);
  s += F("\"><div class=\"col-sm-12\"><span id=\"pwmatch\"></span></div></div></div><div class=\"form-group\"><div class=\"col-sm-offset-2 col-sm-10\"><div class=\"checkbox\"><label><input type=\"checkbox\" name=\"WSL\"");
  s += (WiFi_Status) ? " checked" : "";
  s += F("> Deactive the WiFi LED</label></div></div></div><div class=\"form-group\"><div class=\"col-sm-offset-2 col-sm-10\"><div class=\"checkbox\"><label><input type=\"checkbox\" name=\"dhcp\" id=\"dhcp\" onclick=\"myFunction()\"");
  (dhcp) ? s += " checked" : "";
  s += F("> DHCP</label></div></div></div><div id=\"ipc\"");
  s += (dhcp) ? F(" style=\"display:none\"") : F(" style=\"display:block\"");
  s += F("><div class=\"form-group\"><label class=\"control-label col-sm-2\" for=\"ip\">IP Address:</label><div class=\"col-sm-4\"><input  name=\"ip\" type=\"text\" class=\"form-control\" id=\"ip\" placeholder=\"192.168.1.100\" maxlength=\"15\" value=\"");
  s += String(staticIP[0]) + "." + String(staticIP[1]) + "." + String(staticIP[2]) + "." + String(staticIP[3]);
  s += F("\"></div></div><div class=\"form-group\"><label class=\"control-label col-sm-2\" for=\"gtw\">Gateway:</label><div class=\"col-sm-4\"><input  name=\"gtw\" type=\"text\" class=\"form-control\" id=\"gtw\" placeholder=\"192.168.1.1\" maxlength=\"15\" value=\"");
  s += String(gateway[0]) + "." + String(gateway[1]) + "." + String(gateway[2]) + "." + String(gateway[3]);
  s += F("\"></div></div><div class=\"form-group\"><label class=\"control-label col-sm-2\" for=\"sbnt\">Subnet Mask:</label><div class=\"col-sm-4\"><input  name=\"sbnt\" type=\"text\" class=\"form-control\" id=\"sbnt\" placeholder=\"255.255.255.0\" maxlength=\"15\" value=\"");
  s += String(subnet[0]) + "." + String(subnet[1]) + "." + String(subnet[2]) + "." + String(subnet[3]);
  s += F("\"></div></div><div class=\"form-group\"><label class=\"control-label col-sm-2\" for=\"dns\">DNS1:</label><div class=\"col-sm-4\"><input  name=\"dns\" type=\"text\" class=\"form-control\" id=\"dns\" placeholder=\"8.8.8.8\" maxlength=\"15\" value=\"");
  s += String(dns[0]) + "." + String(dns[1]) + "." + String(dns[2]) + "." + String(dns[3]);
  s += F("\"></div></div><div class=\"form-group\"><label class=\"control-label col-sm-2\" for=\"dns2\">DNS2:</label><div class=\"col-sm-4\"><input  name=\"dns2\" type=\"text\" class=\"form-control\" id=\"dns2\" placeholder=\"8.8.8.8\" maxlength=\"15\" value=\"");
  s += String(dns2[0]) + "." + String(dns2[1]) + "." + String(dns2[2]) + "." + String(dns2[3]);
  s += F("\"></div></div></div> <div class=\"form-group\"> <div class=\"col-sm-offset-2 col-sm-10\"> <button type=\"submit\" name=\"submit\" id=\"submit\" class=\"btn btn-primary\">SAVE</button>&ensp;&ensp;&ensp; <a href=\"/dashboard\" class=\"btn btn-warning\" role=\"button\">Cancel</a> </div> </div> </form> </div> <script type=\"text/javascript\">function funsel(){var e = document.getElementById(\"ssidselect\");var strUser = e.options[e.selectedIndex].text; document.getElementById(\"ssid\").value = e.options[e.selectedIndex].text;}</script> <script>function myFunction() { var checkBox = document.getElementById(\"dhcp\"); var text = document.getElementById(\"ipc\"); if (checkBox.checked == false){ text.style.display = \"block\";document.getElementById(\"ip\").disabled = false;document.getElementById(\"gtw\").disabled = false;document.getElementById(\"sbnt\").disabled = false;document.getElementById(\"dns\").disabled = false;document.getElementById(\"dns2\").disabled = false; } else {text.style.display = \"none\";document.getElementById(\"ip\").disabled = true;document.getElementById(\"gtw\").disabled = true;document.getElementById(\"sbnt\").disabled = true;document.getElementById(\"dns\").disabled = true;document.getElementById(\"dns2\").disabled = true; }}</script> <script>window.onload = function() { var x = document.getElementById(\"dhcp\").checked; document.getElementById('ip').disabled = x;document.getElementById('gtw').disabled = x;document.getElementById('sbnt').disabled = x;document.getElementById('dns').disabled = x;document.getElementById('dns2').disabled = x;};</script>");
  msgForWifiSetting = "";
  return s;
}



String makePage(String title, String content, boolean menu) {

  if (checkLoginFlag()) {
    delay(50);
  }
  //Serial.println(String(content.length()));
  String s = F("<!DOCTYPE html> <html lang=\"en\"> <head> <meta charset=\"utf-8\"> <meta http-equiv=\"X-UA-Compatible\" content=\"IE=edge\"> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"> <title>RAHANIK ESP8266 Dashboard</title> <!-- Bootstrap and custom core CSS --> <link rel=\"stylesheet\" href=\"/bootstrap.min.css\"> <link rel=\"stylesheet\" href=\"/mycss.css\"> <style> body { padding-top: 20px; padding-bottom: 20px; } .navbar { margin-bottom: 20px; } </style> </head> <body> <div class=\"container\"> <!-- Static navbar --> <nav class=\"mymenubar1\"> <div class=\"container-fluid\"> <div class=\"navbar-header\"> <a class=\"navbar-brand\" style=\"color:#FE9804\" href=\"#\">Rahanik ESP8266 Dashboard</a>");
  if (menu) {
    s += makeMenu();
  }
  s += F("</div></div><!--/.container-fluid --> </nav><div class=\"mycontent\"><h1>");
  s += title;
  s += F("</h1><p>");
  s += content;
  s += F("</p></div><!-- Footer --><div class=\"myfooter\"> Firmware Version ");
         s += String(frm_version);
         s += F("<a href=\"http://www.rahanik.ir\" target=\"_blank\" style=\"color:#BBD2E6\"><h4>WWW.RAHANIK.IR</h4></a></div><!-- / Footer --></div><!-- /container --> <!-- Menu JavaScript --> <script>var selectEl = document.getElementById('redirectSelect');selectEl.onchange = function(){window.location = this.value;};</script></body></html>");
  return s;
}



//////////////
// Decoder //
////////////
String urlDecode(String input) {
  String s = input;
  s.replace("%20", " ");
  //s.replace("+", " ");  // Version 37
  s.replace("%21", "!");
  s.replace("%22", "\"");
  s.replace("%23", "#");
  s.replace("%24", "$");
  s.replace("%25", "%");
  s.replace("%26", "&");
  s.replace("%27", "\'");
  s.replace("%28", "(");
  s.replace("%29", ")");
  s.replace("%30", "*");
  s.replace("%31", "+");
  s.replace("%2C", ",");
  s.replace("%2E", ".");
  s.replace("%2F", "/");
  s.replace("%2C", ",");
  s.replace("%3A", ":");
  s.replace("%3A", ";");
  s.replace("%3C", "<");
  s.replace("%3D", "=");
  s.replace("%3E", ">");
  s.replace("%3F", "?");
  s.replace("%40", "@");
  s.replace("%5B", "[");
  s.replace("%5C", "\\");
  s.replace("%5D", "]");
  s.replace("%5E", "^");
  s.replace("%5F", "-");
  s.replace("%60", "`");
  s.replace("\"", "\\\"");
  return s;
}

//////////////////
// Wipe EEPROM //
////////////////
void wipe_eeprom()     // Wipe EEPROM
{
  //EEPROM.begin(512);
  // wipe 1 to 71 bytes of the EEPROM
  for (uint8_t i = 1; i < 73; i++)
    EEPROM.write(i, '|');
  EEPROM.commit();
}



//////////////////
// Read EEPROM //
////////////////
bool read_eeprom_init()     // read init in eeprom 21
{
  // read cell 21 byte of the EEPROM
  bool res = false;
  if (EEPROM.read(21) == '1') {
    res = true;
  }
  delay(20);
  Serial.println("read Init 21 on EEPROM => " + String(res));
  return res;
}



//////////////////
// Write EEPROM //
////////////////
void write_eeprom_init()     // write 1 in eeprom 21
{
  // write 1 in 21 of the EEPROM
  EEPROM.write(21, '1');
  EEPROM.commit();
  delay(20);
  Serial.println(F("Write Init in EEPROM"));
}

// Scan WiFi //
//////////////
void ScanWiFi() {
  if(WiFi.getMode() == WIFI_AP){
    WiFi.mode(WIFI_AP_STA);
  }
  
  uint8_t n = WiFi.scanNetworks(false, true);
  delay(100);
  ////Serial.println(F("Scan WiFi Funtion"));
  SSID_LIST = "";
  for (uint8_t i = 0; i < n; ++i) {
    SSID_LIST += "<option value=\"";
    SSID_LIST += WiFi.SSID(i);
    SSID_LIST += "\">";
    SSID_LIST += WiFi.SSID(i);
    SSID_LIST += "</option>";
  }
  if(WiFi.getMode() == WIFI_AP_STA){
    WiFi.mode(WIFI_AP);
  }
}

void getRSSI() {
  int rssi = WiFi.RSSI();
  STA_RSSI = String(rssi) + "dB";
}

boolean loadSavedConfig() {
  Serial.println(F("Reading Saved Config...."));
  //Load Settings
  fetchSetting();
  delay(10);
  if (!dhcp) {
    //(void)wifi_softap_dhcps_stop();
    WiFi.config(staticIP, gateway, subnet, dns);
    delay(50);
  } else {
    Serial.println("DHCP START");
    (void)wifi_station_dhcpc_start();
  }
  delay(100);
  WiFi.begin(ssid2, password);
  WiFi.setAutoReconnect(true);
  return true;
}

// check WiFi Connection //
//////////////////////////
boolean checkWiFiConnection() {
  uint8_t count = 0;
  Serial.print(F("Waiting to connect to the specified WiFi network"));
  while ( count < 30 ) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println(F(" Connected!"));
      //JUST_GREEN();
      Connected = true;
      delay(500);
      IPAddress ip = WiFi.localIP();
      LOCAL_IP = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
      Serial.println(LOCAL_IP);
      return true;
    }
    delay(350);
    Serial.print(".");
    count++;
  }
  Connected = false;
  Serial.println(F("Timed out."));
  return false;
}

boolean checkWiFiConnectionFast() {
  uint8_t count = 0;
  Serial.print(F("Waiting to connect to the specified WiFi network"));
  while ( count < 18 ) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println(F(" Connected!"));
      //Connected = true;
      IPAddress ip = WiFi.localIP();
      LOCAL_IP = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
      Serial.println(LOCAL_IP);
      return true;
    }
    delay(400);
    Serial.print(".");
    count++;
  }
  //Connected = false;
  Serial.println(F("Timed out."));
  return false;
}

// Build the SSID list and setup a software access point for setup mode
void setupMode() {  // Set AP/Station mode
  if (WiFi.status() != WL_CONNECTED) {
      if(millis() >= wifi_connection_time && wifi_connection_time != 0 && wifi_connection_times <= 5  ){
        WiFi.disconnect();
        WiFi.begin(ssid2, password);
        WiFi.mode(WIFI_AP_STA);

        delay(300);
        if(checkWiFiConnectionFast()){
          Serial.println("------------    Connectred  --------------");
          WiFi.mode(WIFI_STA);
          Connected = true;
          wifi_connection_times = 1; 
          wifi_connection_time = 0;
          Serial.println(F("On Station Mode"));
        }else{
          WiFi.mode(WIFI_AP);
        }
        delay(500);
        if(wifi_connection_times < 5 ){
          wifi_connection_times++;
          wifi_connection_time = millis() + (wifi_connection_times * 300000); // 10min = 600,000 ms 300000   // Try only 3 times in first 30 min by these interval 5min + 10min + 15min
          Serial.println("====>> Current: " + String(millis()) +" - Check wifi_connection_time: " + String(wifi_connection_time) + " - wifi_connection_times: " + String(wifi_connection_times));
          WiFi.printDiag(Serial); // Version 37 - Check Network Details
          Serial.println("===> END of setupMode ");
        }else{
          wifi_connection_time = millis() + (wifi_connection_times * 300000); // 10min = 600,000 ms 300000   // Try only 3 times in first 30 min by these interval 5min + 10min + 15min
          Serial.println("====>> Current: " + String(millis()) +" - Check wifi_connection_time: " + String(wifi_connection_time) + " - wifi_connection_times: +5");
          WiFi.printDiag(Serial); // Version 37 - Check Network Details
          Serial.println("===> END 2 of setupMode ");
        }
      }else if(wifi_connection_time == 0){  // first check
        wifi_connection_time = millis() + (wifi_connection_times * 300000); // 10min = 600,000 ms 300000
        Serial.println("====>>First Check Current: " + String(millis()) +" - Check wifi_connection_time: " + String(wifi_connection_time) + " - wifi_connection_times: " + String(wifi_connection_times));
        //Serial.println("===> END of setupMode ");
        WiFi.printDiag(Serial); // Version 37 - Check Network Details
        
      }
     
  }else{
    if(WiFi.getMode() != 1){
      // GO to Station Mode in Loop Check
      Serial.println("===> setupMode() Check WiFi.getMode() != WIFI_STA");
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      Connected = true;

      wifi_connection_times = 1; // version 38  // Number of try
      wifi_connection_time = 0; // version 38   // Time of next check
        /*
        IPAddress ip = WiFi.localIP();
        LOCAL_IP = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
        Serial.println(LOCAL_IP);
        */
      Serial.println(F("On Station Mode"));
      WiFi.printDiag(Serial); // Version 37 - Check Network Details
      Serial.println("RSSI: " + String(WiFi.RSSI())+" dBm");  // Version 37
      delay(20);
    }
  }
}

void StartAccessPoint(){
  WiFi.mode(WIFI_AP);
  delay(20);
  Serial.println(F("On AP Mode"));
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID.c_str(), AP_PASS.c_str());
  Start_AP = true;
  Serial.print(F("Starting Access Point at \""));
  Serial.println(AP_SSID + "\"");
  //if (!WiFi_Status){digitalWrite(LED_WIFI, HIGH);}
  if (!WiFi_Status){Flash_LED(LED_WIFI, 100, 600, 0);} //Flash_LED(LED,tOn, tOff,One of the previousMillis);
  WiFi.printDiag(Serial); // Version 37 - Check Network Details
}

long connectonTime() { // Calculate connection time in seccond
  long currentUnix = 0;
  if (Begin_Connection_Time < millis() and Begin_Connection_Time != 0) {
    currentUnix = millis() - Begin_Connection_Time;
  } else if (Begin_Connection_Time == 0) {
    return 0;
  }
  return currentUnix / 1000;  //Convert to seccond
}

///////////////////
// LED Function //
/////////////////
void Flash_LED(uint8_t led, int tOn, int tOff, uint8_t array) {
  static int timer = tOn;
  if ((millis() - previousMillis[array]) >= timer) {
    if (digitalRead(led) == HIGH) {
      timer = tOff;
    } else {
      timer = tOn;
    }
    digitalWrite(led, !digitalRead(led));
    previousMillis[array] = millis();
    //Serial.println("LED-WIFI (Connect) => " + String(digitalRead(LED_WIFI)));
  }
}


void getFlash() {
  uint32_t realSize = ESP.getFlashChipRealSize();
  uint32_t ideSize = ESP.getFlashChipSize();
  FlashMode_t ideMode = ESP.getFlashChipMode();

  Serial.printf("Flash real size: %u bytes\n\n", realSize);
  Serial.printf("Flash ide  size: %u bytes\n", ideSize);
  Serial.printf("Flash ide speed: %u Hz\n", ESP.getFlashChipSpeed());
  Serial.println("heap = ESP.getFreeHeap()=>" + String(ESP.getFreeHeap()));
}

void StringToIp(String ipaddress) {
  String input = ipaddress;
  uint8_t idx1  = input.indexOf('.');
  temp_ip[0] = input.substring(0, idx1).toInt();
  uint8_t idx2  = input.indexOf('.', idx1 + 1);
  temp_ip[1] = input.substring(idx1 + 1, idx2).toInt();
  uint8_t idx3  = input.indexOf('.', idx2 + 1);
  temp_ip[2] = input.substring(idx2 + 1, idx3).toInt();
  temp_ip[3] = (input.substring(idx3 + 1)).toInt();
}

void writeInTempconf() {
  File f;
  if (SPIFFS.exists("/tempconf.txt")) {
    SPIFFS.remove("/tempconf.txt");
    ////Serial.println("SPIFFS.exists(tempconf.txt)");
  }
  f = SPIFFS.open("/tempconf.txt", "a");
  ////Serial.println(F("Wrtinging in tempconf.txt ..."));
  f.println("WSL," + String(WiFi_Status));
  //Serial.println("Save WSL,"+String(WiFi_Status));
  f.println("ssid," + String(ssid2));
  //Serial.println("Save ssid,"+String(ssid2));
  f.println("ssidpass," + String(password));
  //Serial.println("Save ssidpass,"+String(password));
  f.println("IP," + String(staticIP[0]) + "." + String(staticIP[1]) + "." + String(staticIP[2]) + "." + String(staticIP[3]));
  //Serial.println("Save IP,"+String(staticIP[0])+"."+String(staticIP[1])+"."+String(staticIP[2])+"."+String(staticIP[3]));
  f.println("GTW," + String(gateway[0]) + "." + String(gateway[1]) + "." + String(gateway[2]) + "." + String(gateway[3]));
  //Serial.println("Save GTW,"+String(gateway[0])+"."+String(gateway[1])+"."+String(gateway[2])+"."+String(gateway[3]));
  f.println("SBNT," + String(subnet[0]) + "." + String(subnet[1]) + "." + String(subnet[2]) + "." + String(subnet[3]));
  //Serial.println("Save SBNT,"+String(subnet[0])+"."+String(subnet[1])+"."+String(subnet[2])+"."+String(subnet[3]));
  f.println("DNS," + String(dns[0]) + "." + String(dns[1]) + "." + String(dns[2]) + "." + String(dns[3]));
  f.println("DNS2," + String(dns2[0]) + "." + String(dns2[1]) + "." + String(dns2[2]) + "." + String(dns2[3]));
  //Serial.println("Save DNS,"+String(dns[0])+"."+String(dns[1])+"."+String(dns[2])+"."+String(dns[3]));
  f.println("DHCP," + String(dhcp));
  //Serial.println("Save DHCP,"+String(dhcp));
  ////Serial.println(F("Writing in tempconf.txt file is done"));
  f.close();
}





void Init() { // used for first starting to format SPIFSS and EEPROM
    if(!read_eeprom_init()){
    wipe_eeprom();
    SPIFFS.format();
    //delay(2000);
    Serial.println("SPIFFS formated");
    write_eeprom_init();
    }
}


void update_online() { // /update Update online the firmeware
  if (checkLoginFlag()) {
    delay(50);
    return;
  }
  if ((millis() - flag_update_click) > 10000) {
    ESPhttpUpdate.setLedPin(LED_WIFI, LOW); // Version 37
    ESPhttpUpdate.onStart(update_started);   // Version 37
    ESPhttpUpdate.onError(update_error);   // Version 38
    
    t_httpUpdate_return ret = ESPhttpUpdate.update(UPDATE_SERVER, frm_version);
    switch (ret) {
      case HTTP_UPDATE_FAILED:
        WEB_SERVER.send(200, "text/html", makePage("Updating Firmware Failed", "<div class=\"mycontentbody\"><h4 class=\"text-danger\">Update Error " + (String)ESPhttpUpdate.getLastError() + ": " + ESPhttpUpdate.getLastErrorString().c_str() + "</h4></div>", true));
        break;
      case HTTP_UPDATE_NO_UPDATES:
        WEB_SERVER.send(200, "text/html", makePage("Update Firmware", "<div class=\"mycontentbody\"><h4 class=\"text-primary\">There is No Update Available.</h4></div>", true));
        break;
      case HTTP_UPDATE_OK:
        WEB_SERVER.send(200, "text/html", makePage("Updating Firmware Done", "<div class=\"mycontentbody\"><h4 class=\"text-success\">Updating Done successfully</h4></div>", true));
        break;
    }
    flag_update_click = millis();
  }
}

String makeUpdateProcessing(){
  String t1 = F("<script type=\"text/javascript\"> function Redirect() { window.location=\"http://");
  t1 += LOCAL_IP;
  t1 += F("\"; } document.write('<div class=\"mycontentbody\"><h4 class=\"text-success\">Please wait...</h4><br>You will be redirected to the <a href=\"http://");
  t1 += LOCAL_IP;
  t1 += "\"> " + LOCAL_IP ;
  t1 += F("</a> after the finish update process.</div>'); setTimeout('Redirect()', 30000); </script></body></html>");
  return(t1);
}

void update_started() { // Version 37
  //Serial.println(F("CALLBACK:  HTTP update process started"));
  WEB_SERVER.send(200, "text/html",makePage("Updating Firmware...",makeUpdateProcessing(),true));
}


void update_error(int err) {
  //Serial.printf("CALLBACK:  HTTP update fatal error code %d\n", err);
  WEB_SERVER.send(200, "text/html", makePage("Updating Firmware Failed", "<div class=\"mycontentbody\"><h4 class=\"text-danger\">Update Error " + (String)ESPhttpUpdate.getLastError() + ": " + ESPhttpUpdate.getLastErrorString().c_str() + "</h4></div>", true));
}



void reboot() { // Reboot Device
  if (checkLoginFlag()) {
    delay(20);
    return;
  }
  WEB_SERVER.sendHeader("Location", "/", true);  //Redirect to our html web page
  WEB_SERVER.send(302, "text/plane", "");
  delay(20);
  ESP.restart();
}


void onAddPin()
{
    //Se passaram os argumentos necessários no post
    if(WEB_SERVER.hasArg("pinName") && WEB_SERVER.hasArg("pinNumber"))
    {
        //Gpio do pino que quer adicionar
        int pinNumber = WEB_SERVER.arg("pinNumber").toInt();

        //Se o pino não estiver em uso
        if(!pins[pinNumber].isInUse)
        {
            //Variável com o nome do botão vinculado ao pino
            char name[MAX_PIN_NAME];
            String argPinName = WEB_SERVER.arg("pinName");
            //Copia o nome do argumento para a variável
            argPinName.toCharArray(name, MAX_PIN_NAME);
            strcpy(pins[pinNumber].name, name);
            //Marca o pino como em uso, para poder aparecer na lista
            pins[pinNumber].isInUse = true;
            //Salva em arquvio as configurações dos pinos
            saveConfig();
            //Retorna sucesso
            WEB_SERVER.send(200, "text/plain", "OK");
        }
        //Pino já está em uso
        else
        {
            //Retorna aviso sobre pino em uso
            WEB_SERVER.send(406, "text/plain", "Not Acceptable - Pin is not Available");
        }
    }
    //Parâmetros incorretos
    else
    {
        //Retorna aviso sobre parâmetros incorretos
        WEB_SERVER.send(400, "text/plain", "Bad Request - Missing Parameters");
    }
}

//Função executada quando fizerem uma requisição POST na rota '/removePin'
void onRemovePin()
{
    //Se o parâmetro foi passado
    if(WEB_SERVER.hasArg("pinNumber"))
    {
        //Verifica qual o gpio vai ficar livre
        int pinNumber = WEB_SERVER.arg("pinNumber").toInt();
        //Marca o pino para dizer que não está mais em uso, fazendo com que ele não apareça mais na lista
        pins[pinNumber].isInUse = false;
        //Salva as configurações em arquivo
        saveConfig();
        //Retorna sucesso
        WEB_SERVER.send(200, "text/plain", "OK");
    }
    else
    {
        //Retorna aviso sobre parâmetro faltando
        WEB_SERVER.send(400, "text/plain", "Bad Request - Missing Parameters");
    }
}

//Função que será executada quando fizerem uma requsição do tipo GET na rota 'pinList'
void onPinList()
{
    //Cria um json que inicialmente mostra qual o valor máximo de gpio
    String json = "{\"count\":"+String(MAX_PIN_COUNT);
    //Lista de pinos
    json += ",\"pins\":[";
    for(int i=0; i < MAX_PIN_COUNT; i++)
    {
        //Se o pino está marcado como em uso
        if(pins[i].isInUse)
        {
            //Adiciona no json as informações sobre este pino
            json += "{";
            json += "\"name\":\"" + String(pins[i].name) + "\",";
            json += "\"number\":" + String(i) + ",";
            json += "\"value\":" + String(pins[i].value);
            json += "},";
        }
    }
    json += "]}";
    //Remove a última virgula que não é necessário após o último elemento
    json.replace(",]}", "]}");
    //Retorna sucesso e o json
    WEB_SERVER.send(200, "text/json", json);
}

void onDigitalWrite()
{
    if(WEB_SERVER.hasArg("pinNumber") && WEB_SERVER.hasArg("pinValue"))
    {
        int number = WEB_SERVER.arg("pinNumber").toInt();
        int value = WEB_SERVER.arg("pinValue").toInt();
        value = value <= 0 ? 0 : 1;
        pinMode(number, OUTPUT);
        digitalWrite(number, value);
        pins[number].value = value;
        saveConfig();
        WEB_SERVER.send(200, "text/plain", "OK");
    }
    else
    {
        WEB_SERVER.send(400, "text/plain", "Bad Request - Missing Parameters");
    }
}

void saveConfig()
{
    File f = SPIFFS.open("/config.bin", "w");
    f.write((uint8_t *)pins, sizeof(Pin)*MAX_PIN_COUNT);
    //Fechamos o arquivo
    f.close();
}

void loadConfig()
{
    File f;
    if((f = SPIFFS.open("/config.bin", "r")) != NULL)
    {
        f.read((uint8_t *)pins, sizeof(Pin)*MAX_PIN_COUNT);
        f.close();
    }

    for(int i=0; i<MAX_PIN_COUNT; i++)
    {
        if(pins[i].isInUse)
        {
            pinMode(i, OUTPUT);
            digitalWrite(i, pins[i].value);
        }
    }
}

void onNotFound() 
{
    WEB_SERVER.send(404, "text/plain", "Not Found");
}
