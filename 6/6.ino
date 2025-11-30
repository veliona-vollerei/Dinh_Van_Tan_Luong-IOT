#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <DFRobotDFPlayerMini.h>
#include <HardwareSerial.h>
#include <HTTPClient.h>

#define MAX_USERS 50
#define MAX_LOG 7

// ===== WiFi ===== 
const char *ssid = "TOTO";
const char *password = "O123456789";

// ===== Admin =====
const String ADMIN_UID = "54:4D:C9:05";
bool adminAuthorized = false;
unsigned long adminTimer = 0;  // 60s

// ===== User Struct =====
struct User {
  int id;
  String name;
  String department;
  String role;
  String uid;
};
User users[MAX_USERS];
int userCount = 0;
int nextId = 1;

// ===== Log chấm công =====
struct LogEntry {
  String name;
  String department;
  String role;
  String uid;
  String imageBase64;
};
LogEntry logs[MAX_LOG];
int logCount = 0;

// ===== Trạng thái chờ quét thẻ =====
bool waitingRegister = false;
int registerIndex = -1;
enum RegisterMode { NONE, NEW_USER, EDIT_UID };
RegisterMode currentMode = NONE;

// ===== Web server + WebSocket =====
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ===== DFLPlayer =====
HardwareSerial dfSerial(1);
DFRobotDFPlayerMini myDFPlayer;

// ===== ESP32-CAM URL =====
String camUrl = "http://192.168.0.15/capture";

// =============================
// TÌM USER THEO UID
int findUserByUID(String uid) {
  for (int i = 0; i < userCount; i++)
    if (users[i].uid == uid) return i;
  return -1;
}

// =============================
// TÌM USER THEO ID
int findUserById(int id) {
  for (int i = 0; i < userCount; i++)
    if (users[i].id == id) return i;
  return -1;
}

// =============================
// Gửi data tới client WebSocket
void notifyClients(String msg) { ws.textAll(msg); }

// =============================
// Phát âm thanh theo sự kiện
void playSound(String event) {
  if (event == "OK") myDFPlayer.play(1);
  else if (event == "FAIL") myDFPlayer.play(2);
  else if (event == "REGISTER_DONE") myDFPlayer.play(3);
  else if (event == "DELETE_DONE") myDFPlayer.play(4);
  else if (event == "EDIT_DONE") myDFPlayer.play(5);
  else if (event == "ADMIN") myDFPlayer.play(6);
}

// =============================
// Lấy ảnh từ ESP32-CAM
String getCamImage() {
  HTTPClient http;
  http.begin(camUrl);
  int code = http.GET();
  String imageData = "";
  if (code == 200) imageData = http.getString();
  http.end();
  return imageData;
}

// =============================
// Thêm vào lịch sử chấm công (Circular buffer)
void addLog(String name, String department, String role, String uid, String imageBase64) {
  if (uid == ADMIN_UID) return; // không lưu log admin
  if (logCount < MAX_LOG) {
    logs[logCount].name = name;
    logs[logCount].department = department;
    logs[logCount].role = role;
    logs[logCount].uid = uid;
    logs[logCount].imageBase64 = imageBase64;
    logCount++;
  } else {
    for (int i = 0; i < MAX_LOG - 1; i++) logs[i] = logs[i + 1];
    logs[MAX_LOG - 1].name = name;
    logs[MAX_LOG - 1].department = department;
    logs[MAX_LOG - 1].role = role;
    logs[MAX_LOG - 1].uid = uid;
    logs[MAX_LOG - 1].imageBase64 = imageBase64;
  }
}

// =============================
// Xử lý UID nhận từ UNO
void processUID(String uid) {
  uid.trim();
  if (uid.length() == 0) return;

  if (uid == ADMIN_UID) {
    adminAuthorized = true;
    adminTimer = millis();
    notifyClients("ADMIN");
    Serial2.println("ADMIN");
    playSound("ADMIN");
    return;
  }

  if (waitingRegister && registerIndex >= 0) {
    users[registerIndex].uid = uid;
    waitingRegister = false;
    registerIndex = -1;

    if (currentMode == NEW_USER) {
      ws.textAll("REGISTER_DONE");
      Serial2.println("REGISTER_DONE");
      playSound("REGISTER_DONE");
    } else if (currentMode == EDIT_UID) {
      ws.textAll("EDIT_DONE");
      Serial2.println("EDIT_DONE");
      playSound("EDIT_DONE");
    }
    currentMode = NONE;
    return;
  }

  // Chấm công bình thường
  int idx = findUserByUID(uid);
  if (idx < 0) {
    notifyClients("UNREGISTERED");
    Serial2.println("FAIL");
    playSound("FAIL");
  } else {
    String img = getCamImage();
    addLog(users[idx].name, users[idx].department, users[idx].role, users[idx].uid, img);
    ws.textAll("USER:" + users[idx].name + "," + users[idx].department + "," + users[idx].role + "," + users[idx].uid + "," + img);
    Serial2.println("OK");
    playSound("OK");
  }
}

// =============================
// WebSocket event
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {}

// =============================
// SETUP
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, 16, 17);
  dfSerial.begin(9600, SERIAL_8N1, 4, 5);

  if (!myDFPlayer.begin(dfSerial)) {
    Serial.println("Khởi tạo DFPlayer thất bại!");
    while (true);
  }
  myDFPlayer.volume(20);
  Serial.println("DFPlayer sẵn sàng.");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) Serial.print(".");
  delay(300);
  Serial.println("\nConnected! IP: " + WiFi.localIP().toString());

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // ===============================
  // XUẤT EXCEL
  server.on("/export_logs", HTTP_GET, [](AsyncWebServerRequest *request){
    String csv = "\xEF\xBB\xBF"; // BOM UTF-8
    csv += "STT,Tên,Phòng,Chức vụ,UID\n";
    for(int i=0;i<logCount;i++){
        csv += String(i+1) + "," + logs[i].name + "," + logs[i].department + "," + logs[i].role + "," + logs[i].uid + "\n";
    }
    request->send(200, "text/csv", csv);
  });

  // ===============================
  // TRANG QUÉT THẺ
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = R"rawliteral(
<!doctype html>
<html lang='vi'>
<head>
<meta charset='utf-8'>
<title>Quét thẻ</title>
<style>
body { font-family: Arial, sans-serif; background:#f2f2f2; margin:0; padding:0; }
h2 { text-align:center; margin:20px 0; color:#333; }
#result { width:320px; margin:20px auto; text-align:center; font-size:18px; background:#fff; padding:15px; border-radius:8px; box-shadow:0 2px 5px rgba(0,0,0,0.2); }
button { display:block; margin:10px auto; padding:10px 20px; font-size:16px; background:#4285f4; color:#fff; border:none; border-radius:5px; cursor:pointer; transition:0.3s; }
button:hover { background:#3367d6; }
img { border-radius:6px; margin-top:10px; }
</style>
</head>
<body>
<h2>Trang quét thẻ</h2>
<div id='result'>Đang chờ quét...</div>
<button onclick="location.href='/admin'">Trang quản lý</button>

<script>
var ws = new WebSocket('ws://'+location.hostname+'/ws');
var timer = null;
ws.onmessage = function(e){
  var msg = e.data;
  var r = document.getElementById('result');
  if(msg=="UNREGISTERED") r.innerHTML="<b style='color:red'>Thẻ chưa đăng ký</b>";
  else if(msg.startsWith("USER:")){
    var parts = msg.substring(5).split(",");
    r.innerHTML="<b>Đã đăng ký:</b><br>Họ tên: "+parts[0]+"<br>Phòng: "+parts[1]+"<br>Chức vụ: "+parts[2]+"<br><img src='data:image/jpeg;base64,"+parts[4]+"' width='200'>";
  } else if(msg=="REGISTER_DONE") console.log("REGISTER_DONE");
  else if(msg=="ADMIN") r.innerHTML="<b style='color:green'>Chào Admin!</b>";
  if(timer) clearTimeout(timer);
  timer=setTimeout(function(){r.innerHTML="Đang chờ quét...";},3000);
};
</script>
</body>
</html>
)rawliteral";
    request->send(200,"text/html",html);
  });

  // ===============================
  // TRANG ADMIN
  server.on("/admin", HTTP_GET, [](AsyncWebServerRequest *request){
    if(!adminAuthorized || millis()-adminTimer>60000){
      String html = "<!doctype html><html><body><h2>Vui long quet the Admin de truy cap</h2>";
      html += "<script>var ws=new WebSocket('ws://'+location.hostname+'/ws');ws.onmessage=function(e){if(e.data=='ADMIN') location.reload();}</script>";
      html += "</body></html>";
      request->send(200,"text/html",html);
      return;
    }

    String html = R"rawliteral(
<!doctype html>
<html lang='vi'>
<head>
<meta charset='utf-8'>
<title>Admin</title>
<style>
body { font-family: Arial, sans-serif; background:#f2f2f2; margin:0; padding:0; }
h2,h3 { text-align:center; color:#333; margin-top:20px; }
button { display:inline-block; margin:5px; padding:8px 15px; font-size:14px; background:#4285f4; color:#fff; border:none; border-radius:5px; cursor:pointer; transition:0.3s; }
button:hover { background:#3367d6; }
table { border-collapse: collapse; width:90%; margin:20px auto; background:#fff; border-radius:8px; overflow:hidden; }
th, td { border:1px solid #ddd; padding:8px; text-align:center; }
th { background:#4285f4; color:#fff; }
tr:hover { background:#f1f1f1; }
img { border-radius:6px; cursor:pointer; }
#overlay { display:none; position:fixed; top:0; left:0; width:100%; height:100%; background:rgba(0,0,0,0.8); text-align:center; }
#overlayImg { max-width:90%; max-height:90%; margin-top:50px; }
#overlay span { position:absolute; top:10px; right:20px; color:white; font-size:30px; cursor:pointer; }
</style>
</head>
<body>
<h2>Quản lý chấm công</h2>
<div style='text-align:center;'>
<button onclick="location.href='/register'">Đăng ký thẻ mới</button>
<button onclick="location.href='/'">Trở về quét</button>
<button onclick="location.href='/export_logs'">Xuất Excel (CSV)</button>
</div>
<h3>Danh sách user</h3>
<table>
<tr><th>ID</th><th>Tên</th><th>Phòng</th><th>Chức vụ</th><th>UID</th><th>Chức năng</th></tr>)rawliteral";

    for(int i=0;i<userCount;i++){
      html += "<tr><td>"+String(users[i].id)+"</td><td>"+users[i].name+"</td><td>"+users[i].department+"</td><td>"+users[i].role+"</td><td>"+users[i].uid+"</td>";
      html += "<td><button onclick=\"location.href='/edit?id="+String(users[i].id)+"'\">Sửa</button>";
      html += "<button onclick=\"location.href='/delete?id="+String(users[i].id)+"'\">Xóa</button></td></tr>";
    }

    html += R"rawliteral(
</table>
<h3>Lịch sử chấm công</h3>
<table id='logTable'>
<tr><th>STT</th><th>Tên</th><th>Phòng</th><th>Chức vụ</th><th>UID</th><th>Ảnh</th></tr>)rawliteral";

    for(int i=0;i<logCount;i++){
      html += "<tr><td>"+String(i+1)+"</td><td>"+logs[i].name+"</td><td>"+logs[i].department+"</td><td>"+logs[i].role+"</td><td>"+logs[i].uid+"</td>";
      html += "<td><img src='data:image/jpeg;base64,"+logs[i].imageBase64+"' width='60' onclick='showImage(this.src)'></td></tr>";
    }

    html += R"rawliteral(
</table>
<div id='overlay'><span onclick='closeOverlay()'>&times;</span><img id='overlayImg'></div>
<script>
function showImage(src){ document.getElementById('overlayImg').src=src; document.getElementById('overlay').style.display='block'; }
function closeOverlay(){ document.getElementById('overlay').style.display='none'; }

var ws = new WebSocket('ws://'+location.hostname+'/ws');
const MAX_LOG = 7;
ws.onmessage=function(e){
  var msg = e.data;
  if(msg.startsWith("LOG:")){
    var parts = msg.substring(4).split(",");
    var table = document.getElementById('logTable');
    if(table.rows.length-1 >= MAX_LOG) table.deleteRow(1);
    var row = table.insertRow(1);
    row.insertCell(0).innerHTML=1;
    for(var j=0;j<5;j++) row.insertCell(j+1).innerHTML=parts[j];
    row.insertCell(6).innerHTML="<img src='data:image/jpeg;base64,"+parts[4]+"' width='60' onclick='showImage(this.src)'>";
    for(var i=1;i<table.rows.length;i++) table.rows[i].cells[0].innerHTML=i;
  }
};
</script>
</body></html>
)rawliteral";

    request->send(200,"text/html",html);
  });

  // ===============================
  // TRANG ĐĂNG KÝ THẺ
  server.on("/register", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = R"rawliteral(
<!doctype html>
<html lang='vi'>
<head>
<meta charset='utf-8'>
<title>Đăng ký thẻ</title>
<style>
body{ font-family: Arial; background:#f2f2f2; text-align:center; padding-top:30px;}
input{padding:8px; margin:5px; border-radius:4px; border:1px solid #ccc;}
button{padding:10px 20px; margin:10px; border:none; border-radius:5px; background:#4285f4; color:#fff; cursor:pointer; transition:0.3s;}
button:hover{background:#3367d6;}
</style>
</head>
<body>
<h2>Đăng ký thẻ mới</h2>
<form action='/register' method='POST'>
Tên: <input name='name'><br>
Phòng ban: <input name='department'><br>
Chức vụ: <input name='role'><br>
<input type='submit' value='Tiếp tục quét thẻ'>
</form>
<button onclick="location.href='/admin'">Quay lại</button>
</body></html>
)rawliteral";
    request->send(200,"text/html",html);
  });

  // POST REGISTER → chờ quét
 server.on("/register", HTTP_POST, [](AsyncWebServerRequest *request){
      String n = request->getParam("name", true)->value();
      String d = request->getParam("department", true)->value();
      String r = request->getParam("role", true)->value();

      users[userCount].id = nextId++;
      users[userCount].name = n;
      users[userCount].department = d;
      users[userCount].role = r;
      users[userCount].uid = "";

      registerIndex = userCount;
      waitingRegister = true;
      currentMode = NEW_USER;
      userCount++;

      String waitPage = "<!doctype html><html><body style='text-align:center;font-family:Arial;'><h2>Vui long quet the de dang ký...</h2>";
      waitPage += "<button onclick=\"location.href='/admin'\">Quay ve trang quan ly</button>";
      waitPage += "<script>var ws=new WebSocket('ws://'+location.hostname+'/ws');ws.onmessage=function(ev){if(ev.data=='REGISTER_DONE'){alert('dang ky thanh cong!'); window.location.href='/admin';}}</script></body></html>";

      request->send(200,"text/html",waitPage);
  });

  // ===============================
  // SỬA USER
 // ===============================
// TRANG SỬA USER
server.on("/edit", HTTP_GET, [](AsyncWebServerRequest *request){
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

    String html = "<!doctype html><html lang='vi'><head><meta charset='utf-8'><title>Sửa User</title>";
    html += "<style>";
    html += "body{font-family:Arial;text-align:center;padding:40px;background:#f4f4f4;}";
    html += "h2{color:#333;}";
    html += "input, button{padding:10px;margin:10px;border-radius:6px;border:1px solid #ccc;font-size:16px;}";
    html += "button{background:#4285f4;color:white;border:none;cursor:pointer;transition:0.3s;}";
    html += "button:hover{background:#3367d6;}";
    html += "#status{margin-top:20px;font-size:18px;color:#555;}";
    html += "</style></head><body>";

    html += "<h2>Sửa thông tin người dùng</h2>";
    html += "<form action='/edit' method='POST'>";
    html += "<input type='hidden' name='id' value='" + String(users[idx].id) + "'>";
    html += "Tên: <input name='name' value='" + users[idx].name + "'><br>";
    html += "Phòng ban: <input name='department' value='" + users[idx].department + "'><br>";
    html += "Chức vụ: <input name='role' value='" + users[idx].role + "'><br>";
    html += "<input type='submit' value='Lưu thay đổi'>";
    html += "</form>";

    html += "<p>UID hiện tại: <b>" + users[idx].uid + "</b></p>";
    html += "<button onclick=\"location.href='/edit_uid?id=" + String(id) + "'\">Quét thẻ để đổi UID</button><br>";
    html += "<button onclick=\"location.href='/admin'\">Quay lại Trang quản lý</button>";

    html += "</body></html>";
    request->send(200,"text/html",html);
});

// ===============================
// TRANG ĐỔI UID
 server.on("/edit", HTTP_GET, [](AsyncWebServerRequest *request){
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

      // Mẫu form sửa (không cho sửa UID ở đây)
      String html = "<!doctype html><html><head><meta charset='utf-8'><title>Sửa user</title></head><body>";
      html += "<h2>Sửa người dùng</h2>";
      html += "<form action='/edit' method='POST'>";
      html += "<input type='hidden' name='id' value='"+String(users[idx].id)+"'>";
      html += "Tên: <input name='name' value='"+users[idx].name+"'><br>";
      html += "Phòng ban: <input name='department' value='"+users[idx].department+"'><br>";
      html += "Chức vụ: <input name='role' value='"+users[idx].role+"'><br>";
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
  // ===============================
    static const char edit_uid_page[] PROGMEM = R"rawliteral(
      <!doctype html>
      <html><head><meta charset='utf-8'><title>Edit UID</title></head><body>
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
      </body></html>
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




  server.on("/edit", HTTP_POST, [](AsyncWebServerRequest *request){
      if(!request->hasParam("id", true)){ request->redirect("/admin"); return; }
      int id = request->getParam("id", true)->value().toInt();
      int idx = findUserById(id);
      if(idx<0){ request->redirect("/admin"); return; }

      String n = request->getParam("name", true)->value();
      String d = request->getParam("department", true)->value();
      String r = request->getParam("role", true)->value();
      String u = request->getParam("uid", true)->value();

      users[idx].name = n;
      users[idx].department = d;
      users[idx].role = r;
      users[idx].uid = u;

      ws.textAll("EDIT_DONE");
      playSound("EDIT_DONE");
      request->redirect("/admin");
  });

  // ===============================
  // XÓA USER
  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request){
      if(!request->hasParam("id")){ request->redirect("/admin"); return; }
      int id = request->getParam("id")->value().toInt();
      int idx = findUserById(id);
      if(idx<0){ request->redirect("/admin"); return; }

      for(int i=idx;i<userCount-1;i++) users[i] = users[i+1];
      userCount--;
      ws.textAll("DELETE_DONE");
      playSound("DELETE_DONE");
      request->redirect("/admin");
  });

  // ===============================
  // Khởi động server
  server.begin();
  Serial.println("Server started");
}

void loop() {
  // Xử lý Serial từ UNO
  if(Serial2.available()){
    String uid = Serial2.readStringUntil('\n');
    processUID(uid);
  }

  // Reset quyền admin sau 60s
  if(adminAuthorized && millis()-adminTimer>60000) adminAuthorized = false;

  delay(10);
}

