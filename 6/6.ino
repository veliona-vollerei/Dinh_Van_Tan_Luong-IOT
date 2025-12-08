/*
  ESP32-DEV_FULL.ino
  Full integration:
    - Serial2 <-> UNO
    - DFPlayer sounds
    - Web admin + WebSocket
    - ESP32-CAM capture integration (IP: 192.168.0.11)
    - Lịch sử chấm công (circular buffer)
*/

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <DFRobotDFPlayerMini.h>
#include <HardwareSerial.h>
#include <HTTPClient.h>

#define MAX_USERS 50
#define MAX_LOG 7

// WiFi
const char *ssid = "TOTO";
const char *password = "O123456789";

// ESP32-CAM URL (you provided)
String camUrl = "http://192.168.0.11/capture";

// Admin UID
const String ADMIN_UID = "54:4D:C9:05";
bool adminAuthorized = false;
unsigned long adminTimer = 0; // 60s

// User struct
struct User {
  int id;
  String name;
  String department;
  String role;
  String uid; // card uid string (if any)
};

User users[MAX_USERS];
int userCount = 0;
int nextId = 1;

// Log chấm công
struct LogEntry {
  String name;
  String department;
  String role;
  String uid;
  String imageBase64; // base64 jpeg
  String ts;
};
LogEntry logs[MAX_LOG];
int logCount = 0;

// Trạng thái chờ quét thẻ/vân tay
bool waitingRegister = false;
bool waitingRegisterFP = false; // nếu true thì chờ vân tay enroll
int registerIndex = -1;

// Web server + websocket
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// DFLPlayer
HardwareSerial dfSerial(1);  // UART1 trên ESP32
DFRobotDFPlayerMini myDFPlayer;

// Serial2 to UNO (RX2=16, TX2=17)
#define SERIAL2_RX 16
#define SERIAL2_TX 17

// =============================
// Anti-spam last scan
unsigned long lastScanTime = 0;
const unsigned long SCAN_DEBOUNCE_MS = 1200; // 1.2s between scans

// =============================
// TÌM USER THEO UID (card)
int findUserByUID(String uid) {
  for(int i=0;i<userCount;i++){
    if(users[i].uid == uid) return i;
  }
  return -1;
}

// =============================
// TÌM USER THEO ID (user.id)
int findUserById(int id){
  for(int i=0;i<userCount;i++){
    if(users[i].id == id) return i;
  }
  return -1;
}

// =============================
// Gửi data tới client
void notifyClients(String msg){
  ws.textAll(msg);
}

// =============================
// Phát âm thanh theo sự kiện
void playSound(String event){
  if(event=="OK") myDFPlayer.play(1);            // Chấm công thành công
  else if(event=="FAIL") myDFPlayer.play(2);    // Chấm công thất bại
  else if(event=="REGISTER_DONE") myDFPlayer.play(3); // Đăng ký thành công
  else if(event=="DELETE_DONE") myDFPlayer.play(4);   // Xóa thành công
  else if(event=="EDIT_DONE") myDFPlayer.play(5);     // Sửa thành công
  else if(event=="ADMIN") myDFPlayer.play(6);         // Chào admin
}

// =============================
// Lấy ảnh từ ESP32-CAM (JPEG base64) – ĐÃ FIX FULL
String getCamImage(){
  HTTPClient http;
  http.setTimeout(6000); // 6s timeout
  if(!http.begin(camUrl)){
    Serial.println("[CAM] begin() failed");
    return "";
  }
  int code = http.GET();
  if(code != 200){
    Serial.println("[CAM] HTTP GET failed, code=" + String(code));
    http.end();
    return "";
  }
  String img = http.getString();
  http.end();

  // Fix base64 newline issues
  img.replace("\n", "");
  img.replace("\r", "");
  return img;
}

// =============================
// Thêm vào lịch sử chấm công (Circular buffer)
String nowTimestamp(){
  time_t t = time(nullptr);
  struct tm *tminfo = localtime(&t);
  char buf[32];
  if(tminfo){
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
      tminfo->tm_year + 1900, tminfo->tm_mon + 1, tminfo->tm_mday,
      tminfo->tm_hour, tminfo->tm_min, tminfo->tm_sec);
  } else {
    snprintf(buf, sizeof(buf), "%lu", millis()/1000);
  }
  return String(buf);
}

void addLog(const String &name, const String &department, const String &role, const String &uid, const String &imageBase64) {
  if(uid == ADMIN_UID) return; // don't log admin
  LogEntry entry;
  entry.name = name;
  entry.department = department;
  entry.role = role;
  entry.uid = uid;
  entry.imageBase64 = imageBase64;
  entry.ts = nowTimestamp();

  if(logCount < MAX_LOG){
    logs[logCount] = entry;
    logCount++;
  } else {
    // shift left
    for(int i=0;i<MAX_LOG-1;i++) logs[i] = logs[i+1];
    logs[MAX_LOG-1] = entry;
  }

  // send WS event for new log. Use delimiter '|' between info and image to avoid splitting issues
  String payload = "LOG:" + entry.name + "," + entry.department + "," + entry.role + "," + entry.uid + "," + entry.ts + "|" + entry.imageBase64;
  notifyClients(payload);
}

// =============================
// Serial2 read buffer
String serial2Buf = "";

// Helpers: read line with timeout (ms)
String readSerial2LineWithTimeout(unsigned long timeoutMs){
  unsigned long start = millis();
  String buf = "";
  while(millis() - start < timeoutMs){
    while(Serial2.available()){
      char c = (char)Serial2.read();
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

// Blocking helper used only in specific endpoints (with timeouts)
String sendCommandAndWaitResponse(String cmd, unsigned long timeoutMs){
  // send a command to UNO and wait for any single-line response within timeout
  Serial2.print(cmd);
  Serial2.println();
  unsigned long start = millis();
  String buf = "";
  while(millis() - start < timeoutMs){
    while(Serial2.available()){
      char c = (char)Serial2.read();
      if(c == '\n'){
        buf.trim();
        return buf;
      } else if(c != '\r'){
        buf += c;
      }
    }
    delay(10);
  }
  buf.trim();
  return buf; // may be empty on timeout
}

// =============================
// Handle a single line from UNO
void handleSerial2Line(String line){
  line.trim();
  if(line.length()==0) return;
  Serial.println("[ESP32] FROM UNO: " + line);

  // Throttle repeated scans
  if(millis() - lastScanTime < SCAN_DEBOUNCE_MS){
    Serial.println("[ESP32] Ignored quick repeat scan");
    return;
  }
  lastScanTime = millis();

  if(line == "ADMIN"){
    adminAuthorized = true;
    adminTimer = millis();
    notifyClients("ADMIN");
    playSound("ADMIN");
    return;
  }
  if(line == "REGISTER_DONE"){
    notifyClients("REGISTER_DONE");
    playSound("REGISTER_DONE");
    return;
  }
  if(line == "EDIT_DONE"){
    notifyClients("EDIT_DONE");
    playSound("EDIT_DONE");
    return;
  }
  if(line == "DELETE_DONE"){
    notifyClients("DELETE_DONE");
    playSound("DELETE_DONE");
    return;
  }
  if(line == "FP_FAIL"){
    // UNO reports no fingerprint match when requested
    notifyClients("UNREGISTERED");
    playSound("FAIL");
    return;
  }

  if(line.startsWith("FP_ID:")){
    int id = line.substring(6).toInt();
    int idx = findUserById(id);
    if(idx < 0){
      Serial.println("[ESP32] FP match but user not found id=" + String(id));
      Serial2.println("FAIL"); // respond to UNO
      notifyClients("UNREGISTERED");
      playSound("FAIL");
      return;
    } else {
      // matched by fingerprint -> capture image, add log, notify clients
      String img = getCamImage();
      addLog(users[idx].name, users[idx].department, users[idx].role, users[idx].uid, img);

      // send USER message with image; use '|' to separate info and image
      String data = "USER:" + users[idx].name + "," + users[idx].department + "," + users[idx].role + "|" + img;
      notifyClients(data);

      Serial2.println("OK"); // phản hồi cho UNO
      playSound("OK");
      return;
    }
  }

  // If looks like card UID (contains ':' and not too long)
  if(line.indexOf(':') != -1 && line.length() < 120){
    String uid = line;
    uid.trim();
    Serial.println("[ESP32] Card UID received: " + uid);

    if(uid == ADMIN_UID){
      adminAuthorized = true;
      adminTimer = millis();
      notifyClients("ADMIN");
      Serial2.println("ADMIN");
      playSound("ADMIN");
      return;
    }

    if(waitingRegister && registerIndex >= 0){
      users[registerIndex].uid = uid;
      waitingRegister = false;
      int assignedId = users[registerIndex].id;
      // if also requested FP registration, send ENROLL:<id> to UNO
      if(waitingRegisterFP){
        waitingRegisterFP = false;
        Serial2.print("ENROLL:");
        Serial2.println(String(assignedId));
      }
      registerIndex = -1;
      Serial2.println("REGISTER_DONE");
      notifyClients("REGISTER_DONE");
      playSound("REGISTER_DONE");
      return;
    }

    int idx = findUserByUID(uid);
    if(idx < 0){
      notifyClients("UNREGISTERED");
      Serial2.println("FAIL");
      playSound("FAIL");
    } else {
      // matched by UID -> capture image, add log, notify clients
      String img = getCamImage();
      addLog(users[idx].name, users[idx].department, users[idx].role, users[idx].uid, img);

      String data = "USER:" + users[idx].name + "," + users[idx].department + "," + users[idx].role + "|" + img;
      notifyClients(data);

      Serial2.println("OK");
      playSound("OK");
    }
    return;
  }

  // unknown message -> ignore
}

// Non-blocking: read available chars and handle full lines
void readSerial2AndHandle(){
  while(Serial2.available()){
    char c = (char)Serial2.read();
    if(c == '\n'){
      handleSerial2Line(serial2Buf);
      serial2Buf = "";
    } else if(c != '\r'){
      serial2Buf += c;
    }
  }
}

// WebSocket stub
void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client,
               AwsEventType type, void * arg, uint8_t *data, size_t len){
  // not needed per-client for now
}

// =============================
// SETUP + HTTP pages
void setup(){
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, SERIAL2_RX, SERIAL2_TX);  // RX2 = GPIO16, TX2 = GPIO17 (UNO)
  dfSerial.begin(9600, SERIAL_8N1, 4, 5);   // RX=4, TX=5 nối với DFPlayer

  if(!myDFPlayer.begin(dfSerial)){
    Serial.println("Khởi tạo DFPlayer thất bại!");
    while(true);
  }
  myDFPlayer.volume(20);
  Serial.println("DFPlayer sẵn sàng.");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Connecting WiFi");
  while(WiFi.status() != WL_CONNECTED){
    Serial.print(".");
    delay(300);
  }
  Serial.println("\nConnected!");
  Serial.println("IP: " + WiFi.localIP().toString());

  // initialize time (for timestamps) - optional: try to get NTP quickly
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // === ROOT / quét thẻ
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    static const char index_html[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
<meta charset='utf-8'>
<title>Quét thẻ</title>
<style>
  body { font-family: Arial, sans-serif; background:#f0f2f5; margin:0; padding:0; display:flex; flex-direction:column; align-items:center;}
  h2 { color:#333; margin-top:20px; }
  #result { margin-top:20px; padding:15px; background:white; border-radius:8px; box-shadow:0 2px 5px rgba(0,0,0,0.1); min-width:300px; text-align:center;}
  button { margin-top:15px; padding:10px 20px; border:none; border-radius:5px; background:#007bff; color:white; font-size:14px; cursor:pointer;}
  button:hover { background:#0056b3; }
</style>
</head>
<body>
<h2>Trang quét thẻ</h2>
<div id='result'>Đang chờ quét...</div>
<button onclick="location.href='/admin'">Trang quản lý</button>
<script>
  var ws = new WebSocket('ws://'+location.hostname+'/ws');
  var timer=null;
  ws.onmessage=function(e){
    var msg=e.data;
    var r=document.getElementById('result');
    if(msg=="UNREGISTERED") r.innerHTML="<b style='color:red'>Chưa đăng ký</b>";
    else if(msg.startsWith("USER:")){
      var raw = msg.substring(5);
      var parts = raw.split("|");
      var info = parts[0].split(",");
      var img = parts.length>1?parts[1]:"";
      r.innerHTML="<b>Đã đăng ký:</b><br>Tên:"+info[0]+"<br>Phòng:"+info[1]+"<br>Chức vụ:"+info[2];
      if(img && img.length>10){
        r.innerHTML += "<br><br><img src='data:image/jpeg;base64,"+img+"' width='300' style='border-radius:8px;box-shadow:0 2px 5px rgba(0,0,0,0.2);'>";
      }
    } else if(msg=="REGISTER_DONE") console.log("REGISTER_DONE");
    else if(msg=="ADMIN") r.innerHTML="<b style='color:green'>Chào Admin!</b>";
    if(timer) clearTimeout(timer);
    timer=setTimeout(function(){r.innerHTML="Đang chờ quét...";},3000);
  };
</script>
</body>
</html>
)rawliteral";

      request->send_P(200,"text/html",index_html);
  });

  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request){
    request->redirect("/");
  });

  // ===============================
  // TRANG ADMIN
  server.on("/admin", HTTP_GET, [](AsyncWebServerRequest *request){
 if(!adminAuthorized || millis() - adminTimer > 60000){
    request->redirect("/");
    return;
}

    String html = "<!doctype html><html><head><meta charset='utf-8'><title>Admin</title>";
        html += "<style>"
        "body{font-family:Arial,sans-serif;background:#f5f6fa;margin:0;padding:20px;}"
        "h2{color:#333;}"
        "button{padding:8px 15px;margin:5px;border:none;border-radius:5px;background:#28a745;color:white;cursor:pointer;}"
        "button:hover{background:#218838;}"
        "table{width:100%;border-collapse:collapse;margin-top:20px;}"
        "th,td{padding:10px;text-align:center;border-bottom:1px solid #ddd;}"
        "th{background:#007bff;color:white;}"
        "tr:hover{background:#f1f1f1;}"
        "img{border-radius:5px;}"
        "#overlay{display:none;position:fixed;top:0;left:0;width:100%;height:100%;background-color:rgba(0,0,0,0.8);text-align:center;}"
        "#overlayImg{max-width:90%;max-height:90%;margin-top:50px;}"
        "#overlay span{color:white;position:absolute;top:10px;right:20px;cursor:pointer;font-size:30px;}"
        "</style>";
        html += "</head><body>";
        html += "<h2>Quản lý chấm công</h2>";
        html += "<button onclick=\"location.href='/register'\">Đăng ký thẻ mới</button>";
        html += "<button onclick=\"location.href='/'\">Trở về quét</button>";
        html += "<button onclick=\"location.href='/export_logs'\">Xuất Excel (CSV)</button>";

        // Bảng user
        html += "<table><tr><th>ID</th><th>Tên</th><th>Phòng</th><th>Chức vụ</th><th>UID</th><th>Chức năng</th></tr>";
        for(int i=0;i<userCount;i++){
          html += "<tr>";
          html += "<td>"+String(users[i].id)+"</td>";
          html += "<td>"+users[i].name+"</td>";
          html += "<td>"+users[i].department+"</td>";
          html += "<td>"+users[i].role+"</td>";
          html += "<td>"+users[i].uid+"</td>";
          html += "<td>";
          html += "<button onclick=\"location.href='/edit?id="+String(users[i].id)+"'\">Sửa</button>";
          html += "<button onclick=\"location.href='/delete?id="+String(users[i].id)+"'\">Xóa</button>";
          html += "</td></tr>";
        }
        html += "</table>";

        // Bảng log
        html += "<h3>Lịch sử chấm công</h3>";
        html += "<table id='logTable'><tr><th>STT</th><th>Tên</th><th>Phòng</th><th>Chức vụ</th><th>UID</th><th>Thời gian</th><th>Ảnh</th></tr>";
        for(int i=0;i<logCount;i++){
          html += "<tr>";
          html += "<td>"+String(i+1)+"</td>";
          html += "<td>"+logs[i].name+"</td>";
          html += "<td>"+logs[i].department+"</td>";
          html += "<td>"+logs[i].role+"</td>";
          html += "<td>"+logs[i].uid+"</td>";
          html += "<td>"+logs[i].ts+"</td>";
          html += "<td><img src='data:image/jpeg;base64,"+logs[i].imageBase64+"' width='60' style='cursor:pointer' onclick='showImage(this.src)'></td>";
          html += "</tr>";
        }
        html += "</table>";

        // Overlay view ảnh
        html += "<div id='overlay'><span onclick='closeOverlay()'>&times;</span><img id='overlayImg'></div>";
        html += "<script>"
        "function showImage(src){document.getElementById('overlayImg').src=src;document.getElementById('overlay').style.display='block';}"
        "function closeOverlay(){document.getElementById('overlay').style.display='none';}"
        "var ws = new WebSocket('ws://'+location.hostname+'/ws');"
        "const MAX_LOG="+String(MAX_LOG)+";"
        "ws.onmessage=function(e){"
        "var msg=e.data;"
        "if(msg.startsWith('LOG:')){"
        "var raw=msg.substring(4);"
        "var parts=raw.split('|');"
        "var info=parts[0].split(',');"
        "var img=parts.length>1?parts[1]:'';"
        "var table=document.getElementById('logTable');"
        "if(table.rows.length-1>=MAX_LOG)table.deleteRow(1);"
        "var row=table.insertRow(1);"
        "row.insertCell(0).innerHTML=1;"
        "row.insertCell(1).innerHTML=info[0];"
        "row.insertCell(2).innerHTML=info[1];"
        "row.insertCell(3).innerHTML=info[2];"
        "row.insertCell(4).innerHTML=info[3];"
        "row.insertCell(5).innerHTML=info[4];"
        "var c6=row.insertCell(6);"
        "if(img && img.length>10)c6.innerHTML=\"<img src='data:image/jpeg;base64,\"+img+\"' width='60' style='cursor:pointer' onclick='showImage(this.src)'>\"; else c6.innerHTML='';"
        "for(var i=1;i<table.rows.length;i++) table.rows[i].cells[0].innerHTML=i;"
        "} else if(msg=='ADMIN'){location.reload();}"
        "};"
        "</script>";

        html += "</body></html>";

      request->send(200,"text/html",html);
});

  // export logs as CSV
  server.on("/export_logs", HTTP_GET, [](AsyncWebServerRequest *request){
    String csv = "\xEF\xBB\xBF"; // BOM UTF-8
    csv += "STT,Tên,Phòng,Chức vụ,UID,Thời gian\n";
    for(int i=0;i<logCount;i++){
        csv += String(i+1) + "," + logs[i].name + "," + logs[i].department + "," + logs[i].role + "," + logs[i].uid + "," + logs[i].ts + "\n";
    }
    request->send(200, "text/csv", csv);
  });

  // ===============================
  // ĐĂNG KÝ THẺ - FORM (bổ sung checkbox đăng ký vân tay)
 static const char register_form[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
<meta charset='utf-8'>
<title>Đăng ký thẻ</title>
<style>
  body{font-family:Arial;background:#f0f2f5;text-align:center;padding:20px;}
  h2{color:#2196F3;}
  form{background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 8px rgba(0,0,0,0.2);display:inline-block;}
  input[type=text]{padding:6px;width:200px;margin:5px;}
  input[type=submit]{padding:8px 20px;background:#4CAF50;color:white;border:none;border-radius:5px;cursor:pointer;transition:0.2s;}
  input[type=submit]:hover{background:#45a049;}
  button{margin-top:10px;padding:6px 12px;border:none;border-radius:5px;background:#2196F3;color:white;cursor:pointer;}
  button:hover{background:#1976D2;}
  label{font-size:14px;}
</style>
</head>
<body>
<h2>Đăng ký thẻ mới</h2>
<form action='/register' method='POST'>
  Tên:<br> <input name='name' required><br>
  Phòng ban:<br> <input name='department' required><br>
  Chức vụ:<br> <input name='role' required><br>
  <label><input type='checkbox' name='register_fp' value='1'> Đăng ký vân tay</label><br>
  <input type='submit' value='Tiếp tục quét thẻ / vân tay'>
</form>
<br>
<button onclick="location.href='/admin'">Quay lại</button>
</body>
</html>
)rawliteral";

  server.on("/register", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", register_form);

  });

  // ===============================
  // POST REGISTER → chuyển sang chờ quét
  server.on("/register", HTTP_POST, [](AsyncWebServerRequest *request){
      String n = request->getParam("name", true)->value();
      String d = request->getParam("department", true)->value();
      String r = request->getParam("role", true)->value();
      bool wantFP = false;
      if(request->hasParam("register_fp", true)){
        String v = request->getParam("register_fp", true)->value();
        if(v == "1") wantFP = true;
      }

      // thêm user mới, chờ quét UID (card)
      users[userCount].id = nextId++;
      users[userCount].name = n;
      users[userCount].department = d;
      users[userCount].role = r;
      users[userCount].uid = "";

      registerIndex = userCount;
      waitingRegister = true;
      waitingRegisterFP = wantFP;
      userCount++;

      static const char wait_register[] PROGMEM = R"rawliteral(
        <!doctype html>
        <html><head><meta charset='utf-8'><title>Chờ quét</title></head><body>
          <h2>Vui lòng quét thẻ để hoàn tất đăng ký (hoặc admin nhấn nút quét vân tay trên UNO)...</h2>
          <button onclick="location.href='/admin'">Quay về trang quản lý</button>
          <script>
            var ws = new WebSocket('ws://'+location.hostname+'/ws');
            ws.onmessage = function(ev){
                if(ev.data=="REGISTER_DONE"){
                    alert("Đăng ký thành công!");
                    window.location.href='/admin';
                }
            };
          </script>
        </body></html>
      )rawliteral";

      request->send_P(200, "text/html", wait_register); 
  });

  // ===============================
  // SỬA USER – GET
  server.on("/edit", HTTP_GET, [](AsyncWebServerRequest *request){
  if(!request->hasParam("id")){ request->redirect("/admin"); return; }
  int id = request->getParam("id")->value().toInt();
  int idx = findUserById(id);
  if(idx<0){ request->redirect("/admin"); return; }

  String html = "<!doctype html><html><head><meta charset='utf-8'><title>Sửa user</title>";
  html += R"rawliteral(
  <style>
    body{font-family:Arial;background:#f0f2f5;text-align:center;padding:20px;}
    h2{color:#2196F3;}
    form{background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 8px rgba(0,0,0,0.2);display:inline-block;}
    input[type=text]{padding:6px;width:200px;margin:5px;}
    input[type=submit]{padding:8px 20px;background:#4CAF50;color:white;border:none;border-radius:5px;cursor:pointer;transition:0.2s;}
    input[type=submit]:hover{background:#45a049;}
    button{margin-top:10px;padding:6px 12px;border:none;border-radius:5px;background:#2196F3;color:white;cursor:pointer;}
    button:hover{background:#1976D2;}
    label{font-size:14px;}
  </style>
  </head><body>
  <h2>Sửa người dùng</h2>
  )rawliteral";

  html += "<form action='/edit' method='POST'>";
  html += "<input type='hidden' name='id' value='"+String(users[idx].id)+"'>";
  html += "Tên:<br><input name='name' value='"+users[idx].name+"' required><br>";
  html += "Phòng ban:<br><input name='department' value='"+users[idx].department+"' required><br>";
  html += "Chức vụ:<br><input name='role' value='"+users[idx].role+"' required><br>";
  html += "<label><input type='checkbox' name='edit_fp' value='1'> Quét vân tay mới</label><br>";
  html += "<input type='submit' value='Lưu thay đổi'>";
  html += "</form>";
  html += "<p>UID hiện tại: <b>"+users[idx].uid+"</b></p>";
  html += "<button onclick=\"location.href='/edit_uid?id="+String(id)+"'\">Quét thẻ để đổi UID</button><br>";
  html += "<button onclick=\"location.href='/admin'\">Quay lại</button>";
  html += "</body></html>";

  request->send(200,"text/html",html);
});

  // ===============================
  // TRANG CHỜ QUÉT THẺ ĐỂ ĐỔI UID (edit_uid)
  static const char edit_uid_page[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
<meta charset='utf-8'>
<title>Edit UID</title>
<style>
  body{font-family:Arial;background:#f0f2f5;text-align:center;padding:20px;}
  h2{color:#2196F3;}
  button{margin-top:10px;padding:8px 16px;border:none;border-radius:5px;background:#2196F3;color:white;cursor:pointer;transition:0.2s;}
  button:hover{background:#1976D2;}
</style>
</head>
<body>
<h2>Quét thẻ mới để cập nhật UID</h2>
<button onclick="location.href='/admin'">Quay lại</button>
<script>
  var ws = new WebSocket('ws://'+location.hostname+'/ws');
  ws.onmessage = function(ev){
      if(ev.data=="EDIT_DONE"){
          alert("Cập nhật UID thành công!");
          window.location.href='/admin';
      }
  }
</script>
</body>
</html>
)rawliteral";


    server.on("/edit_uid", HTTP_GET, [](AsyncWebServerRequest *request){
        if(!request->hasParam("id")){ request->redirect("/admin"); return; }
        int id = request->getParam("id")->value().toInt();
        int idx = findUserById(id);
        if(idx < 0){ request->redirect("/admin"); return; }

        registerIndex = idx;
        waitingRegister = true;

        request->send_P(200, "text/html", edit_uid_page);
    });

  // ===============================
  // TRANG QUÉT VÂN TAY ĐỂ CẬP NHẬT (edit_fp)
  server.on("/edit_fp", HTTP_GET, [](AsyncWebServerRequest *request){
    if(!request->hasParam("id")){ request->redirect("/admin"); return; }
    int id = request->getParam("id")->value().toInt();
    int idx = findUserById(id);
    if(idx < 0){ request->redirect("/admin"); return; }

    // Send ENROLL:<id> to UNO and return waiting page (UNO sẽ send EDIT_DONE when completed)
    Serial2.print("ENROLL:");
    Serial2.println(String(id));

    String html = "<!doctype html><html><head><meta charset='utf-8'><title>Edit FP</title></head><body>";
    html += "<h2>Đang chờ quét vân tay trên thiết bị UNO để cập nhật...</h2>";
    html += "<button onclick=\"location.href='/admin'\">Quay lại</button>";
    html += "<script>";
    html += "var ws = new WebSocket('ws://'+location.hostname+'/ws');";
    html += "ws.onmessage = function(ev){ if(ev.data=='EDIT_DONE'){ alert('Cập nhật vân tay thành công!'); window.location.href='/admin'; } };";
    html += "</script></body></html>";
    request->send(200,"text/html",html);
  });

  // ===============================
  // LƯU SỬA USER (POST edit)
  server.on("/edit", HTTP_POST, [](AsyncWebServerRequest *request){
      int id = request->getParam("id", true)->value().toInt();
      int idx = findUserById(id);
      if(idx < 0){ request->redirect("/admin"); return; }

      users[idx].name = request->getParam("name", true)->value();
      users[idx].department = request->getParam("department", true)->value();
      users[idx].role = request->getParam("role", true)->value();

      bool wantFP = false;
      if(request->hasParam("edit_fp", true)){
        String v = request->getParam("edit_fp", true)->value();
        if(v == "1") wantFP = true;
      }
      if(wantFP){
        Serial2.print("ENROLL:");
        Serial2.println(String(id));
      }

      myDFPlayer.play(5);
      request->redirect("/admin");
  });

  // ===============================
  // XÓA USER
  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request){
      if(!request->hasParam("id")){
        request->redirect("/admin");
        return;
      }
      int id = request->getParam("id")->value().toInt();
      int idx = findUserById(id);
      if(idx < 0){
        request->redirect("/admin");
        return;
      }

      // ---- GIẢI PHÁP QUAN TRỌNG ----
      // XÓA LUÔN FINGERPRINT TRONG R307
      Serial2.print("DELETE_FP:");
      Serial2.println(String(id));
      // --------------------------------

      // Xóa user trong danh sách
      for(int i=idx; i < userCount-1; i++){
        users[i] = users[i+1];
      }
      userCount--;
       myDFPlayer.play(4);
      request->redirect("/admin");
  });

  server.begin();
}

// =============================
// LOOP – đọc Serial2 (UNO) và xử lý
void loop(){
  ws.cleanupClients();
  readSerial2AndHandle();

  // (Optional) do other periodic tasks here
}
