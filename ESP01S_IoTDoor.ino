#include <Arduino_JSON.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
// #include <EEPROM.h>
#include <FS.h>
#include <map>
#include "html_index.h"

// ============ 烧录注意 ============
/*
烧录如果出现Permission相关问题, 需要下载旧版本串口驱动
开发板: Generic 8266 Module
Upload speed: 921600
Flash size: FS:128KB OTA:~438KB
Erase Flash: Only Sketch
!!!注意, 修改CONFIG_CONFIG_FILE将会导致所有配置丢失, 包括WIFI连接信息, 在OTA时请三思
HTML转gz字节码详见html2bytes.py
*/
// ============ 烧录注意 ============

// ============ 宏定义内容 ============
#define SYS_SOFT_VERSION "2.0.2"

#define DEBUG(...) \
  printf(__VA_ARGS__); \
  fflush(stdout);
#define DEBUGSPLN() \
  for (int i = 0; i < 40; i++) DEBUG("="); \
  DEBUG("\n");
#define DBC(x) const_cast<char*>((const char*)x)
#define CHECKERRCONFIG(x) x.indexOf(CONFIG_CONFIG_FILE) == -1 || x.indexOf("{") == -1 || x.indexOf("}") == -1
// #define HTML_ALERT(srv, x) srv->send(200, "text/html", "<html><script>alert('" x "');</script></html>");
// #define HTML_BACK(srv) srv->send(200, "text/html", "<html><script>window.history.back()</script></html>");

#define DEFAULT_AP_SSID "IOT_DOOR"
#define CONFIG_CONFIG_FILE "config.db"  //Change this to reconfigurate, !!!Change will cause Configuration lost via OTA
#define CONFIG_WIFI_SSID "impossible_ssid"
#define CONFIG_WIFI_PASS "000000"
#define CONFIG_WIFI_CODE -1     //Last error code in WiFi connecting, refer to WiFi.status()
#define CONFIG_WIFI_TIMEOUT 20  //Max spent seconds in WiFi connecting, `0` is forbidden
#define CONFIG_WIFI_MAXTRY 3    //Max attempt times to connect WiFi, switch to AP mode if exceeded, `0` is forbidden
#define CONFIG_SERVO_POS_1 0
#define CONFIG_SERVO_POS_2 50
#define CONFIG_SERVO_DELAY 5
#define CONFIG_OTA_SERVER "http://ota.btnext.cn:5010/door/ota/ota.json"

// ============ 宏定义内容 ============

// ============ 无线网络类 ============
class WLAN {
private:
  byte timeout_tick = 0;
  /**
  * @brief Try to connect to an AP using giving parameters
  * @param[in] ssid Target AP's ssid.
  * @param[in] pass Target AP's password.
  * @param[in] timeout Max seconds to wait to be connected.
  * @return WiFi.status
  */
  int connect(char* ssid, char* pass, byte timeout = 20) {
    // //If no target WiFi nearby, return false.
    // if (scan(ssid, pass) == 0) {
    //   DEBUG("[WLAN]No target AP nearby.\n");
    //   return false;
    // }
    if (timeout == 0) timeout = 20;  //To avoid return directly because of value of 0

    timeout_tick = 0;
    DEBUG("[WLAN]Try to connect to %s\n", ssid);
    WiFi.mode(WIFI_STA);
    strcmp(pass, "") == 0 ? WiFi.begin(ssid) : WiFi.begin(ssid, pass);

    /*
      WL_NO_SHIELD        = 255,   // for compatibility with WiFi Shield library
      WL_IDLE_STATUS      = 0,
      WL_NO_SSID_AVAIL    = 1,
      WL_SCAN_COMPLETED   = 2,
      WL_CONNECTED        = 3,
      WL_CONNECT_FAILED   = 4,
      WL_CONNECTION_LOST  = 5,
      WL_WRONG_PASSWORD   = 6,
      WL_DISCONNECTED     = 7
    */

    //Spin if no connected
    DEBUG("[WLAN]Status:");
    int code = WiFi.status();
    while (code != WL_CONNECTED) {
      DEBUG("%d..", code);
      delay(1000);
      timeout_tick++;

      if (timeout_tick >= timeout) {
        DEBUG("\n[WLAN]Fail to connect, check status code hebind.\n");
        return code;
      }
      if (code == WL_CONNECT_FAILED) timeout_tick = timeout;
      if (code != WL_DISCONNECTED) {
        DEBUG("\n");
        return code;  //If not disconnected status, return it
      }
      code = WiFi.status();
    }
    // setup_mDNS();
    DEBUG("\n[WLAN]Connected, spent %d seconds, IP: %s\n", timeout_tick, WiFi.localIP().toString().c_str());
    return code;
  }
  static void onAPConnected(const WiFiEventSoftAPModeStationConnected& evt) {
    DEBUG("[WLAN]New client connected, aid:%d, mac:%02X%02X%02X%02X%02X%02X\n",
          evt.aid,
          evt.mac[0], evt.mac[1], evt.mac[2], evt.mac[3], evt.mac[4], evt.mac[5]);
  }
  /**
  * @brief Create a AP using DEFAULT_AP_SSID
  */
  void AP() {
    if (WiFi.getMode() == WIFI_AP) return;
    DEBUG("[WLAN]Switch to AP mode.\n");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(DEFAULT_AP_SSID);
    DEBUG("[WLAN]AP-SSID: %s\n", DEFAULT_AP_SSID);
    // setup_mDNS();
  }
  // static void setup_mDNS() {
  //   if (!MDNS.begin("iotdoor", WiFi.localIP())) {
  //     DEBUG("\n[mDNS]Fail to setup mDNS\n");
  //   } else {
  //     MDNS.addService("http", "tcp", 80);
  //     DEBUG("\n[mDNS]mDNS running on `iotdoor.local`\n");
  //   }
  // }

public:
  JSONVar* DBJson;
  WLAN(JSONVar* dj) {
    DBJson = dj;
    WiFi.onSoftAPModeStationConnected(onAPConnected);
  }
  /**
  * @brief Scan APs nearby and check if there is the target AP.
  * @param[in] ssid Target AP's ssid.
  * @param[in] pass Target AP's password.
  * @return True if found target AP, else false.
  */
  // bool scan(char* ssid, char* pass) {
  //   DEBUG("[WLAN]Scaning...\n");
  //   DEBUGSPLN();
  //   int n = WiFi.scanNetworks();     //Sync scan, blocking
  //   bool flag_if_target_ap = false;  //Flag: set to 1 if find same ssid between configuration and scan list
  //   for (int i = 0; i < n; i++) {
  //     DEBUG("[%d]SSID:%s\tCN:%d\tRSSI:%ddBm\tSec:%s\n",
  //           i, WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i),
  //           WiFi.encryptionType(i) == ENC_TYPE_NONE ? "Open" : "Encrypt");

  //     //First check if there is ssid, then check if the encryptionType is the same
  //     if (WiFi.SSID(i).equals(ssid)) {
  //       if (WiFi.encryptionType(i) == ENC_TYPE_NONE && strcmp(pass, "") == 0) flag_if_target_ap = true;
  //       if (WiFi.encryptionType(i) != ENC_TYPE_NONE && strcmp(pass, "") != 0) flag_if_target_ap = true;
  //     }
  //   }
  //   WiFi.scanDelete();  //Clear stored list from ram.
  //   DEBUGSPLN();
  //   return flag_if_target_ap;
  // }


  /**
  * @brief Run this function in loop to check current status of WiFi
  * @update at 2025-01-20 23:54
  * Try to connect again after a while at AP mode.
  */
  void run(byte max_try_times = 20) {
    static byte try_times = 0;
    static unsigned long last_ap_time = 0;  // Record last get in AP mode time

    int wmt = (int)(*DBJson)["wifi_maxtry"];
    if (wmt != 0) {
      max_try_times = wmt;
    }

    // scan();

    //If connected, return
    // DEBUG("Hello1, status: %d, IP: %s\n", WiFi.status(), WiFi.localIP().toString().c_str());
    if (WiFi.status() == WL_CONNECTED) {
      try_times = 0;
      last_ap_time = millis();
      return;
    }
    // DEBUG("Hello2\n");

    // if in AP mode for a long time, try to reset to reconnect. default 2min.
    if (WiFi.getMode() == WIFI_AP && millis() - last_ap_time >= 2 * 60 * 1000) {
      if (WiFi.softAPgetStationNum() == 0) {
        DEBUG("[WLAN]Long time in AP mode, try to switch to STA mode to reconnect.\n");
        try_times = 0;
        last_ap_time = millis();
        return;
      }
      DEBUG("[WLAN]Long time in AP mode, keep on AP because of devices-connected.\n")
      try_times = 0;
      last_ap_time = millis();
      return;
    }

    if (try_times >= max_try_times) {
      if (WiFi.getMode() == WIFI_AP) return;
      DEBUG("\n[WLAN]Exceeded max attempt times, switch to AP mode.\n");
      AP();  //After user saved configuration, the system will restart.
      last_ap_time = millis();
      return;
    }

    //Try to connect...
    WiFi.mode(WIFI_STA);
    DEBUG("[WLAN]Try to connect, try times: %d/%d\n", try_times + 1, max_try_times);
    int wifi_code = connect(
      DBC((*DBJson)["wifi_ssid"]),
      DBC((*DBJson)["wifi_pass"]),
      (int)(*DBJson)["wifi_timeout"]);
    (*DBJson)["wifi_code"] = wifi_code;  //Save to show on web manager page
    /*
      WL_NO_SHIELD        = 255,   // for compatibility with WiFi Shield library
      WL_IDLE_STATUS      = 0,
      WL_NO_SSID_AVAIL    = 1,//AP
      WL_SCAN_COMPLETED   = 2,
      WL_CONNECTED        = 3,
      WL_CONNECT_FAILED   = 4,
      WL_CONNECTION_LOST  = 5,
      WL_WRONG_PASSWORD   = 6,//AP
      WL_DISCONNECTED     = 7
    */
    switch (wifi_code) {
      case WL_NO_SSID_AVAIL:
      case WL_WRONG_PASSWORD:
        {
          //set `try_times` to max number directly
          DEBUG("\n[WLAN]No AP nearby or wrong password, reach max number directly.\n");
          try_times = max_try_times;
          break;
        }
      default:
        {
          try_times += 1;
        }
    }
  }
};
// ============ 无线网络类 ============

// ============ 空中升级类 ============
class OTAme {
public:
  JSONVar* DBJson;
  char* ota_server;
  WiFiClient client;
  OTAme(JSONVar* dj) {
    DBJson = dj;
    ota_server = DBC((*DBJson)["ota_server"]);
    DEBUG("[OTA]OTA server: %s\n", ota_server);
  }

  /**
  * @brief Run this function in loop to check ota version
  */
  void run(unsigned int check_millis) {
    static unsigned long last_check_time = millis();  // Record last check time
    if (!(millis() - last_check_time > check_millis)) return;
    last_check_time = millis();

    // All happen in case of wifi-connected
    if (WiFi.status() != WL_CONNECTED) return;
    DEBUG("[OTA]Checking OTA infomation...\n");

    // Get ota.json
    WiFiClient client;
    HTTPClient http;
    String payload;

    // http://ip-api.com/json/
    http.begin(client, ota_server);
    // http.begin(client, "http://ip-api.com/json/");
    int httpResCode = http.GET();
    if (httpResCode != 200) {
      DEBUG("[OTA]Fail to connect to OTA server: %d\n", httpResCode);
      return;
    }

    payload = http.getString();
    http.end();

    JSONVar respData = JSON.parse(payload);
    if (JSON.typeof(respData) == "undefined") {
      DEBUG("[OTA]Fail to parse json.\n");
      return;
    }

    // Get key-value info
    const char* ota_version = respData["version"];
    const char* ota_bin_url = respData["ota_bin"];

    DEBUG("[OTA]Local version: %s, remote verison: %s\n", SYS_SOFT_VERSION, ota_version);
    if (String(SYS_SOFT_VERSION) == String(ota_version)) return;

    DEBUG("[OTA]Try to upgrade at %s...\n", ota_bin_url);

    ESPhttpUpdate.onProgress(update_progress);
    t_httpUpdate_return ret = ESPhttpUpdate.update(client, String(ota_bin_url));

    switch (ret) {
      case HTTP_UPDATE_FAILED:
        DEBUG("[OTA]HTTP_UPDATE_FAILED Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
        break;
      case HTTP_UPDATE_NO_UPDATES:
        DEBUG("[OTA]HTTP_UPDATE_NO_UPDATES");
        break;
      case HTTP_UPDATE_OK:
        DEBUG("[OTA]HTTP_UPDATE_OK");
        break;
      default:
        DEBUG("[OTA]ESP update error: %d", ret);
    }

    // Serial.println(payload);
    // DEBUG("[OTA]Code: %d, res: %s\n", httpResCode, version);
  }
  void static update_progress(int cur, int total) {
    DEBUG("[OTA]HTTP update process at %d of %d bytes...\n", cur, total);
  }
};
// ============ 空中升级类 ============

// ============ 文件系统类 ============
class FSystem {
public:
  JSONVar* DBJson;
  FSystem(JSONVar* dj) {
    DBJson = dj;
    (*DBJson)["config_file"] = CONFIG_CONFIG_FILE;
    (*DBJson)["wifi_ssid"] = CONFIG_WIFI_SSID;
    (*DBJson)["wifi_pass"] = CONFIG_WIFI_PASS;
    (*DBJson)["wifi_code"] = CONFIG_WIFI_CODE;
    (*DBJson)["wifi_timeout"] = CONFIG_WIFI_TIMEOUT;
    (*DBJson)["wifi_maxtry"] = CONFIG_WIFI_MAXTRY;
    (*DBJson)["servo_position"][0] = CONFIG_SERVO_POS_1;
    (*DBJson)["servo_position"][1] = CONFIG_SERVO_POS_2;
    (*DBJson)["servo_delay"] = CONFIG_SERVO_DELAY;
    (*DBJson)["ota_server"] = CONFIG_OTA_SERVER;

    // DEBUG("[DB]%s\n", JSON.stringify(DBJson).c_str());

    if (!SPIFFS.begin()) {  //Initialize SPIFFS
      DEBUG("[FS]SPIFFS Failed to Start, the process will be shutdown in 3 seconds later.\n");
      delay(3000);
      ESP.restart();
    }

    DEBUG("[FS]SPIFFS started, checking config file...\n");
    //Check if it has been initialized by config file,
    //not just format it and create a default config file.

    if (!SPIFFS.exists(CONFIG_CONFIG_FILE)) {
      DEBUG("[FS]No flag file named `%s`, reinitialize it.\n", CONFIG_CONFIG_FILE);
      SPIFFS.format();
      File f = SPIFFS.open(CONFIG_CONFIG_FILE, "w+");
      if (!f) {
        DEBUG("[FS]Flag file failed to create, the process will be shutdown in 3 seconds later.\n");
        delay(3000);
        ESP.restart();
      }

      //Create default config file
      db_write();

      f.close();
    }

    DEBUG("[FS]Config file existed, try to read...\n");
    if (!db_read()) {
      DEBUG("[FS]Fail to read, try to reset in 3 seconds...\n");
      db_write();
      delay(3000);
      ESP.restart();
    }
  }

  bool db_read() {
    File ff = SPIFFS.open(CONFIG_CONFIG_FILE, "r");
    if (!ff) {
      DEBUG("[FS]Config file failed to create, the process will be shutdown in 3 seconds later.\n");
      delay(3000);
      ESP.restart();
    }

    String str;
    while (ff.available()) {
      str += (char)ff.read();
    }

    // DEBUG("[FS]Config file read: %s\n", str.c_str());

    //todo:IF ERROR, TRY TO DELETE IT AND RESTART CHIP
    if (CHECKERRCONFIG(str)) {
      DEBUG("[FS]Parse error, please check config file.\n", CONFIG_CONFIG_FILE, str);
      ff.close();
      return false;
    }

    *DBJson = JSON.parse(str);
    if (JSON.typeof(*DBJson) == "undefined") {
      DEBUG("[FS]Fail to parse config file json.\n");
      return false;
    }
    DEBUG("[FS]Read config file successfully.\n");
    for (int i = 0; i < (*DBJson).keys().length(); i++) {
      String key = (*DBJson).keys()[i];
      String value = JSON.stringify((*DBJson)[key]);
      DEBUG("[FS]=> %s: %s\n", key.c_str(), value.c_str());
    }

    ff.close();
    return true;
  }

  void db_write() {
    DEBUG("[FS]Writing config file...\n");
    File ff = SPIFFS.open(CONFIG_CONFIG_FILE, "w");
    if (!ff) {
      DEBUG("[FS]Config file failed to create, the process will be shutdown in 3 seconds later.\n");
      delay(3000);
      ESP.restart();
    }
    String str = JSON.stringify(*DBJson);
    if (!ff.print(str.c_str())) {
      DEBUG("[FS]Fail to write config.\n");
    } else {
      DEBUG("[FS]Config has been updated.\n");
    }
    ff.flush();
    ff.close();
  }

  static bool db_write_by_str(String cfg) {
    DEBUG("[FS]Writing config file by string...\n");
    File ff = SPIFFS.open(CONFIG_CONFIG_FILE, "w");
    if (!ff) {
      DEBUG("[FS]Fail to create config file, restarting in 3 seconds...\n");
      delay(3000);
      ESP.restart();
      return false;
    }

    if (CHECKERRCONFIG(cfg)) {
      DEBUG("[FS]Checking config file failed.\n");
      ff.close();
      return false;
    }

    if (!ff.print(cfg.c_str())) {
      DEBUG("[FS]Fail to write config.\n");
      ff.close();
      return false;
    }

    DEBUG("[FS]Config has been updated.\n");
    ff.flush();
    ff.close();
    return true;
  }

  static Dir dir() {
    return SPIFFS.openDir("");
  }

  static FSInfo info() {
    /*  struct FSInfo {
        size_t totalBytes;
        size_t usedBytes;
        size_t blockSize;
        size_t pageSize;
        size_t maxOpenFiles;
        size_t maxPathLength;
      };
    */
    FSInfo fi;
    SPIFFS.info(fi);
    return fi;
  }
};
// ============ 文件系统类 ============

// ============ 硬件操作类 ============
class HardDrive {
private:
  JSONVar* DBJson;
public:
  HardDrive(JSONVar* dj) {
    DBJson = dj;
    /*
    #define INPUT             0x00
    #define INPUT_PULLUP      0x02
    #define INPUT_PULLDOWN_16 0x04 // PULLDOWN only possible for pin16
    #define OUTPUT            0x01
    #define OUTPUT_OPEN_DRAIN 0x03
    #define WAKEUP_PULLUP     0x05
    #define WAKEUP_PULLDOWN   0x07
    #define SPECIAL           0xF8 //defaults to the usable BUSes uart0rx/tx uart1tx and hspi
    */

    io_set(0, false, true);  //PWM
    io_read(2, true, true);  //LED io input-up
  }
  void run() {
    if (io_read(2, true) == 0) {
      open_door();
    }
  }
  void open_door() {
    int sp1 = (*DBJson)["servo_position"][0];
    int sp2 = (*DBJson)["servo_position"][1];
    int sd = (*DBJson)["servo_delay"];
    DEBUG("[HD]Do opening door[%d,%d], delay: %ds\n", sp2, sp1, sd);

    servo_control(0, sp2);
    delay(sd * 1000);
    servo_control(0, sp1);
  }
  static void test(int pin, byte* servo_position) {
    DEBUG("[HD]TESTING servo\n");
    for (int i = int(servo_position[0]); i < int(servo_position[1]); i++) {
      servo_control(pin, i);
      delay(1);
    }
    servo_control(pin, int(servo_position[0]));
    delay(1000);
    // delay(1500);
  }
  static void servo_control(int pin, int percentage, bool debug = false) {
#define SERVO_H_MicroSeconds int(percentage / 100.0 * (2.5 - 0.5) * 1000 + 0.5 * 1000)
#define SERVO_L_MicroSeconds int(20 * 1000 - (percentage / 100.0 * (2.5 - 0.5) * 1000 + 0.5 * 1000))
    byte i = 27;  //To assure that servo has reached target degree
    while (i--) {
      digitalWrite(pin, HIGH);
      delayMicroseconds(SERVO_H_MicroSeconds);
      digitalWrite(pin, LOW);
      delayMicroseconds(SERVO_L_MicroSeconds);
      // DEBUG("[HD]HIGH:%5dus,\t LOW:%5dus,\t TOTAL:%5dus\n", SERVO_H_MicroSeconds, SERVO_L_MicroSeconds, SERVO_H_MicroSeconds + SERVO_L_MicroSeconds);
    }
    if (debug) DEBUG("[HD]Servo[%d]: %d%%\n", pin, percentage);
  }
  static void io_set(int pin, bool value, bool debug = false) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, value);
    if (debug) DEBUG("[HD]IO[%d] set to %s\n", pin, value ? "HIGH" : "LOW");
  }
  static byte io_read(int pin, bool is_pullup = false, bool debug = false) {
    is_pullup ? pinMode(pin, INPUT_PULLUP) : pinMode(pin, INPUT);
    byte value = digitalRead(pin);
    if (debug)
      DEBUG("[HD]IO[%d](%s) read %s\n", pin, is_pullup ? "Pullup" : "Normal", value ? "HIGH" : "LOW");
    return value;
  }
};
// ============ 硬件操作类 ============

// ============ 网页服务类 ============
class WebService {
private:
  JSONVar* DBJson;
  static void reply_ok(ESP8266WebServer* server) {
    server->send(200, "text/plain", "");
  }
  static void not_found(ESP8266WebServer* server) {
    if (server->uri().startsWith("/file/")) {
      api_file(server);
    } else {
      JSONRES(server, 404, "api not found");
    }
  }

  static void JSONRES(ESP8266WebServer* server, int code = 200, String msg = "succ", JSONVar* res = NULL) {
    //code,msg,data
    JSONVar myjson;
    myjson["code"] = code;
    myjson["msg"] = msg;
    if (res) myjson["data"] = *res;
    server->send(200, "application/json", JSON.stringify(myjson));
  }

  static void JSONRES(ESP8266WebServer* server, JSONVar* res) {
    JSONRES(server, 200, "succ", res);
  }
public:
  ESP8266WebServer* srv;
  WebService(JSONVar* dj) {
    DBJson = dj;
    srv = new ESP8266WebServer(80);
    srv->enableCORS(true);
    srv->onNotFound(std::bind(&this->not_found, srv));
    srv->on("/", HTTP_GET, std::bind(&this->homepage, srv));
    srv->on("/file", HTTP_GET, std::bind(&this->api_file, srv));
    srv->on("/api/sys_version", HTTP_GET, std::bind(&this->api_sys_version, srv));
    srv->on("/api/sys_boottime", HTTP_GET, std::bind(&this->api_sys_boottime, srv));
    srv->on("/api/sys_proxy", HTTP_GET, std::bind(&this->api_sys_proxy, srv));
    srv->on("/api/fs_dir", HTTP_GET, std::bind(&this->api_fs_dir, srv));
    srv->on("/api/fs_info", HTTP_GET, std::bind(&this->api_fs_info, srv));
    srv->on("/api/fs_rm", HTTP_GET, std::bind(&this->api_fs_rm, srv));
    srv->on("/api/fs_upload", HTTP_POST, std::bind(&this->reply_ok, srv), std::bind(&this->api_fs_upload, srv));
    srv->on("/api/fs_download", HTTP_GET, std::bind(&this->api_fs_download, srv));
    srv->on("/api/db_pull", HTTP_GET, std::bind(&this->api_db_pull, srv, dj));
    srv->on("/api/db_push", HTTP_GET, std::bind(&this->api_db_push, srv, dj));
    srv->on("/api/restart", HTTP_GET, std::bind(&this->api_restart, srv));
    srv->on("/api/reset", HTTP_GET, std::bind(&this->api_reset, srv));
    srv->on("/api/wl_scan", HTTP_GET, std::bind(&this->api_wl_scan, srv));
    srv->on("/api/io_pwm", HTTP_GET, std::bind(&this->api_io_pwm, srv));
    srv->on("/api/io_set", HTTP_GET, std::bind(&this->api_io_set, srv));
    srv->on("/api/io_read", HTTP_GET, std::bind(&this->api_io_read, srv));
    srv->on("/api/cus_opendoor", HTTP_GET, std::bind(&this->api_cus_opendoor, srv, dj));

    srv->begin();
  }

  static void homepage(ESP8266WebServer* server) {
    DEBUG("[WS]User access homepage.\n");
    server->sendHeader("Content-Encoding", "gzip");
    server->send(200, "text/html", GZHTML_INDEX, GZHTML_INDEX_LEN);
  }
  static void api_cus_opendoor(ESP8266WebServer* server, JSONVar* dj) {
    int sp1 = (*dj)["servo_position"][0];
    int sp2 = (*dj)["servo_position"][1];
    int sd = (*dj)["servo_delay"];
    DEBUG("[WS]Do opening door[%d,%d], delay: %ds\n", sp2, sp1, sd);
    JSONRES(server);

    HardDrive::servo_control(0, sp2);
    delay(sd * 1000);
    HardDrive::servo_control(0, sp1);
  }

  static void api_io_pwm(ESP8266WebServer* server) {
    DEBUG("[WS]User access io_pwm.\n");
    if (!server->hasArg("io") || !server->hasArg("value")) {
      JSONRES(server, 400, "Empty arg");
      return;
    }
    HardDrive::servo_control(server->arg("io").toInt(), server->arg("value").toInt(), true);
    JSONRES(server);
  }


  static void api_io_set(ESP8266WebServer* server) {
    DEBUG("[WS]User access io_set.\n");
    if (!server->hasArg("io") || !server->hasArg("value")) {
      JSONRES(server, 400, "Empty arg");
      return;
    }
    HardDrive::io_set(server->arg("io").toInt(), server->arg("value").toInt());
    JSONRES(server);
  }

  static void api_io_read(ESP8266WebServer* server) {
    DEBUG("[WS]User access io_read.\n");
    if (!server->hasArg("io") || !server->hasArg("type")) {
      JSONRES(server, 400, "Empty arg");
      return;
    }
    JSONVar res;
    res = HardDrive::io_read(server->arg("io").toInt(), server->arg("type").toInt(), true);
    JSONRES(server, &res);
  }

  static void api_restart(ESP8266WebServer* server) {
    DEBUG("[WS]Restart chip in 3 seconds...\n");
    JSONRES(server, 200, "Restart chip in 3 seconds...");

    delay(3000);
    ESP.restart();
  }

  static void api_reset(ESP8266WebServer* server) {
    DEBUG("[WS]Clear config and restart in 3 seconds...\n");

    if (SPIFFS.exists(CONFIG_CONFIG_FILE)) {
      if (!SPIFFS.remove(CONFIG_CONFIG_FILE)) {
        JSONRES(server, 500, "Fail to remove config file.");
        return;
      }
      JSONRES(server, 200, "Clear config and restart in 3 seconds...");
    }

    delay(3000);
    ESP.restart();
  }

  static void api_db_pull(ESP8266WebServer* server, JSONVar* dj) {
    DEBUG("[WS]User access db_pull.\n");
    JSONRES(server, dj);
  }
  static void api_db_push(ESP8266WebServer* server, JSONVar* dj) {
    DEBUG("[WS]User access db_push.\n");

    if (!server->hasArg("data")) {
      DEBUG("[WS]Empty arg.\n");
      JSONRES(server, 400, "Error format");
      return;
    }

    String raw = server->arg("data");
    DEBUG("[WS]Received config: %s\n", raw.c_str())

    if (!FSystem::db_write_by_str(raw)) {
      JSONRES(server, 500, "Fail to write config by string");
      return;
    }

    JSONRES(server, 200, "Config file has been updated, restarting in 3 seconds...");
    DEBUG("[WS]Config file has been updated, restarting in 3 seconds...\n");
    delay(3000);
    ESP.restart();
    return;
  }

  static void api_sys_version(ESP8266WebServer* server) {
    JSONVar version = SYS_SOFT_VERSION;
    JSONRES(server, &version);
  }

  static void api_sys_boottime(ESP8266WebServer* server) {
    JSONVar startMillis = millis();
    JSONRES(server, &startMillis);
  }

  static void api_sys_proxy(ESP8266WebServer* server) {
    WiFiClient client;
    HTTPClient http;
    String url = server->arg("url");
    String method = server->arg("method");
    String body = server->arg("body");
    String payload;

    // http://ip-api.com/json/
    DEBUG("[WS]User access sys_proxy.[%s,%s,%s]\n", url.c_str(), method.c_str(), body.c_str());

    if (url == "") {
      JSONRES(server, 400, "Missing 'url' parameter");
      return;
    }

    if (method == "") {
      method = "GET";
    }

    if (!http.begin(client, url)) {
      JSONRES(server, 500, "Failed to initialize HTTP client");
      return;
    }

    int httpResCode = -1;
    if (method.equalsIgnoreCase("GET")) {
      httpResCode = http.GET();
    } else if (method.equalsIgnoreCase("POST")) {
      httpResCode = http.POST(body);
    } else if (method.equalsIgnoreCase("PUT")) {
      httpResCode = http.PUT(body);
    } else {
      http.end();
      JSONRES(server, 400, "Unsupported HTTP method");
      return;
    }

    if (httpResCode > 0) {
      payload = http.getString();
      server->send(httpResCode, http.header("Content-Type"), payload);
    } else {
      JSONRES(server, 500, String("HTTP request failed code: ") + httpResCode + String(", ") + http.errorToString(httpResCode));
    }

    http.end();
  }

  static void api_file(ESP8266WebServer* server) {
    String fileName = server->uri();
    fileName.remove(0, 6);
    DEBUG("[WS]User access api_file.[%s]\n", fileName.c_str());

    if (fileName.isEmpty()) {
      JSONRES(server, 400, "File name is missing.");
      return;
    }

    if (!SPIFFS.exists(fileName)) {
      JSONRES(server, 404, "File not found.");
      return;
    }

    File ff = SPIFFS.open(fileName, "r");
    if (!ff) {
      JSONRES(server, 500, "File open failed.");
      return;
    }

    String contentType = "application/octet-stream";
    if (fileName.endsWith(".html") || fileName.endsWith(".htm")) contentType = "text/html";
    else if (fileName.endsWith(".css")) contentType = "text/css";
    else if (fileName.endsWith(".js")) contentType = "application/javascript";
    else if (fileName.endsWith(".png")) contentType = "image/png";
    else if (fileName.endsWith(".jpg") || fileName.endsWith(".jpeg")) contentType = "image/jpeg";
    else if (fileName.endsWith(".gif")) contentType = "image/gif";
    else if (fileName.endsWith(".txt")) contentType = "text/plain";

    server->sendHeader("Connection", "close");
    server->streamFile(ff, contentType);

    ff.close();  // 关闭文件
  }


  static void api_fs_dir(ESP8266WebServer* server) {
    DEBUG("[WS]User access fs_dir.\n");

    int i = 0;
    JSONVar myarr;

    Dir dd = FSystem::dir();
    while (dd.next()) {
      myarr[i++] = dd.fileName();
      // DEBUG("%s\n", dd.fileName());
    }

    JSONRES(server, &myarr);
  }

  static void api_fs_rm(ESP8266WebServer* server) {
    DEBUG("[WS]User access fs_rm.\n");
    if (SPIFFS.remove(server->arg("file"))) {
      JSONRES(server);
    } else {
      JSONRES(server, 400, "Fail to remove file");
    }
  }

  static void api_fs_upload(ESP8266WebServer* server) {
    /*  struct FSInfo {
        size_t totalBytes;
        size_t usedBytes;
        size_t blockSize;
        size_t pageSize;
        size_t maxOpenFiles;
        size_t maxPathLength;
      };
    */
    static FSInfo fi;
    static int leftBytes;
    HTTPUpload& upload = server->upload();
    if (upload.status == UPLOAD_FILE_START) {
      DEBUG("[WS]User access fs_upload.\n");
      SPIFFS.info(fi);
      leftBytes = fi.totalBytes - fi.usedBytes;
    }
    if (upload.status == UPLOAD_FILE_WRITE) {
      DEBUG("[WS]Status:%d, name:%s, filename:%s, totalSize:%d, currSize:%d\n",
            upload.status,
            upload.filename,
            upload.name,
            upload.totalSize,
            upload.currentSize);
    }
    // if (upload.totalSize > leftBytes) {
    //   JSONRES(server, 500, "No enough space");
    // }
    switch (upload.status) {
      case UPLOAD_FILE_START:
        {
          DEBUG("[WS]Handle file upload - Filename: %s\n", upload.filename);
          File ff = SPIFFS.open(upload.filename, "w");
          if (!ff) {
            DEBUG("[WS]Failed to open file for writing.\n");
            JSONRES(server, 500, "Failed to open file for writing");
            return;
          }
          DEBUG("[WS]Start to receive file...\n");
          break;
        }

      case UPLOAD_FILE_WRITE:
        {
          DEBUG("[WS]Receiving file: %s\n", upload.filename)
          File ff = SPIFFS.open(upload.filename, "a");
          if (ff) {
            ff.write(upload.buf, upload.currentSize);
            ff.close();
          }
          break;
        }

      case UPLOAD_FILE_END:
        {

          DEBUG("[WS]Received file successfully.\n")
          JSONRES(server);
          break;
        }

      default:
        {
          DEBUG("[WS]Unexpected status, upload status:%d\n", upload.status);
          JSONRES(server, 500, "Unexpected upload status");
        }
    }
  }

  static void api_fs_download(ESP8266WebServer* server) {
    DEBUG("[WS]User access fs_download.\n");

    if (!SPIFFS.exists(server->arg("file"))) {
      JSONRES(server, 400, "File not found.");
      return;
    }

    File ff = SPIFFS.open(server->arg("file").c_str(), "r");
    if (!ff) {
      JSONRES(server, 400, "File open failed.");
      return;
    }

    server->sendHeader("Content-Type", "text/plain");
    server->sendHeader("Content-Disposition", "attachment; filename=" + server->arg("file"));
    server->sendHeader("Connection", "close");
    server->streamFile(ff, "application/octet-stream");

    ff.close();
  }

  static void api_fs_info(ESP8266WebServer* server) {
    DEBUG("[WS]User access fs_info.\n");
    /*  struct FSInfo {
        size_t totalBytes;
        size_t usedBytes;
        size_t blockSize;
        size_t pageSize;
        size_t maxOpenFiles;
        size_t maxPathLength;
        };
    */
    JSONVar myjson;
    FSInfo fs_info = FSystem::info();
    myjson["total"] = fs_info.totalBytes;
    myjson["used"] = fs_info.usedBytes;
    myjson["block"] = fs_info.blockSize;
    myjson["page"] = fs_info.pageSize;
    myjson["maxOpenFiles"] = fs_info.maxOpenFiles;
    myjson["maxPathLength"] = fs_info.maxPathLength;

    // DEBUG("[WS]total: %dB, used: %dB\n", fs_info.totalBytes, fs_info.usedBytes);

    JSONRES(server, &myjson);
  }

  static void api_wl_scan(ESP8266WebServer* server) {
    DEBUG("[WS]User access wl_scan.\n");
    int n = WiFi.scanNetworks();
    JSONVar myjson;
    for (int i = 0; i < n; i++) {
      JSONVar tempjson;
      tempjson["SSID"] = WiFi.SSID(i).c_str();
      tempjson["CN"] = WiFi.channel(i);
      tempjson["RSSI"] = WiFi.RSSI(i);
      tempjson["Sec"] = WiFi.encryptionType(i) == ENC_TYPE_NONE ? "Open" : "Encrypt";
      myjson[i] = tempjson;
    }
    JSONRES(server, &myjson);
    WiFi.scanDelete();  //Release ram
  }

  void run() {
    srv->handleClient();
  }
};
// ============ 网页服务类 ============

// Database* db;
WLAN* wlan;
OTAme* otame;
WebService* websrv;
FSystem* mfs;
HardDrive* hd;
JSONVar DatabaseJson;
void setup() {
  Serial.begin(115200);
  DEBUG("\n");
  delay(5000);
  // db = new Database();        //create class object, @deprecated, new one in FSystem
  mfs = new FSystem(&DatabaseJson);        //create class object
  wlan = new WLAN(&DatabaseJson);          //create class object
  otame = new OTAme(&DatabaseJson);        //create class object
  websrv = new WebService(&DatabaseJson);  //create class object
  hd = new HardDrive(&DatabaseJson);       //create class object

  // db->print();
  // hd->test(0, db->EEPROMDB.servo_position);
  // DEBUG("========%s\n", DBJson["wifi_ssid"]);

  // bool connect_flag = wl->connect(
  //   DBC(DatabaseJson["wifi_ssid"]),
  //   DBC(DatabaseJson["wifi_pass"]));  //Try to connect to AP.
  // if (!connect_flag) {
  //   wl->AP();
  // }

  DEBUG("Hello, btnext!\n");
}

void loop() {
  // Serial.println("Hello, 8266!");
  // db->print();
  hd->run();
  // hd->open_door();
  delay(50);
  // hd->servo_control(0, a);
  // hd->servo_control(2, a);
  // DEBUG("%d",a++);
  // if (a >= 100) a = 0;
  wlan->run();
  // otame->run(10 * 1000);
  otame->run(60 * 1000);  // 60s检测一次
  websrv->run();
}
