📌 HỆ THỐNG CHẤM CÔNG 
🔥 Giới thiệu

Dự án này xây dựng một hệ thống chấm công realtime sử dụng:

ESP32 → Web server + xử lý logic + giao diện quản lý

Arduino UNO + RFID RC522 → đọc UID thẻ

DFPlayer Mini → phát âm thanh thông báo

ESP32-CAM (tuỳ chọn) → chụp ảnh khi chấm công

WebSocket realtime → cập nhật giao diện ngay lập tức

FIFO log (tối đa 7 dòng, tự đẩy vòng)

Xuất Excel UTF-8 không lỗi font tiếng Việt

Chức năng sửa UID an toàn (không bị chấm công nhầm)

Hệ thống phù hợp cho:
✔ Công ty nhỏ
✔ Workshop
✔ Lớp học / phòng lab
✔ Điểm danh nhân viên / học viên

🚀 Tính năng nổi bật
⭐ Chấm công bằng RFID

Quét thẻ → hiển thị thông tin người dùng

Lưu log (Tên – Phòng – Chức vụ – UID – Ảnh nếu có)

FIFO 7 dòng → đầy thì tự cuộn vòng

⭐ Giao diện Web Admin

Thêm / sửa user

Đổi UID bằng quét thẻ mới (không chấm công nhầm)

Xóa user

Quản lý lịch sử chấm công

Xuất Excel CSV chuẩn UTF-8

⭐ Hoạt động Realtime

Nhờ WebSocket, mọi thao tác:

Quét thẻ

Sửa UID

Đăng ký user
→ hiển thị ngay lập tức trên trang web mà không cần reload.

⭐ DFPlayer Mini thông báo

0001 → Chấm công thành công

0002 → Chấm công thất bại

0003 → Đăng ký thành công

0004 → Xóa thành công

0005 → Sửa thành công

0006 → Chào admin

📡 Phần cứng sử dụng
Thiết bị	Vai trò
ESP32 DevKit	Web server + xử lý logic
Arduino UNO	Đọc RFID
RC522	Quét thẻ
DFPlayer Mini	Phát âm thanh
ESP32-CAM (option)	Chụp ảnh chấm công
Nguồn 5V 2A	Cấp nguồn hệ thống

                               

⚙️ Ba chế độ hoạt động chính

Hệ thống dùng biến:

RegisterMode { MODE_NONE, MODE_REGISTER, MODE_EDIT }

1️⃣ MODE_NONE – Chấm công

Mặc định

Quét thẻ → ghi log

2️⃣ MODE_REGISTER – Đăng ký user

Khi thêm user

Quét thẻ → gán UID mới

3️⃣ MODE_EDIT – Đổi UID

Khi sửa UID trong trang admin

Quét thẻ → cập nhật UID (KHÔNG chấm công)

🔄 Luồng đổi UID (hoạt động chuẩn professional)

Người dùng vào

/edit_uid?id=5


Nhấn nút

[ Bắt đầu quét thẻ ]


Web gọi ESP32:

/start_scan?id=5


ESP32 bật:

waitingRegister = true
currentMode = MODE_EDIT
registerIndex = 5


Quét thẻ mới → ESP32:

Cập nhật UID
Gửi EDIT_DONE qua WebSocket


Web báo thành công → quay lại admin

📑 Xuất Excel không lỗi font
Lý do không lỗi font

ESP32 gửi BOM UTF-8:

String csv = "\xEF\xBB\xBF";
csv += "STT,Tên,Phòng,Chức vụ,UID\n";


Excel nhận đúng tiếng Việt.

📁 Cấu trúc log FIFO (7 dòng)
Index 0 → Cũ nhất
Index 6 → Mới nhất


Khi có log thứ 8:

Xóa index 0

Dịch lên

Ghi log vào index 6

🌐 Các endpoint chính
Endpoint	Mô tả
/	Trang chấm công
/admin	Quản lý user
/add_user	Thêm user
/delete?id=	Xóa user
/edit?id=	Sửa user
/edit_uid?id=	Giao diện đổi UID
/start_scan?id=	Bật chế độ quét UID
/export_logs	Xuất Excel CSV
/ws	WebSocket realtime
🖥 Preview giao diện (mô tả)
✔ Trang chấm công

Hiển thị user

Ảnh chụp (nếu có ESP32-CAM)

Ghi log realtime

✔ Trang admin

Bảng user

Nút sửa / xóa

Nút đổi UID

Nút xuất Excel

Giao diện thân thiện, dễ dùng

🔧 Cấu hình âm thanh DFPlayer
File	Ý nghĩa
0001.mp3	Chấm công OK
0002.mp3	Chấm công Fail
0003.mp3	Đăng ký OK
0004.mp3	Xóa OK
0005.mp3	Sửa OK
0006.mp3	Chào admin


🏁 Kết luận

Hệ thống hoạt động ổn định, chuyên nghiệp với:

✔ Chấm công realtime
✔ Quản lý người dùng
✔ Đổi UID không sai log
✔ FIFO log tự động
✔ Xuất Excel đẹp, chuẩn
✔ Giao diện web trực quan
✔ Mã nguồn dễ mở rộng
