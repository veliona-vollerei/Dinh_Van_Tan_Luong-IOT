ĐỀ TÀI: HỆ THỐNG CHẤM CÔNG BẰNG VÂN TAY
1. Lý do chọn đề tài

Trong quá trình quản lý nhân sự và sinh viên tại các cơ quan, doanh nghiệp và cơ sở giáo dục, công tác điểm danh và chấm công đóng vai trò quan trọng trong việc đánh giá hiệu quả làm việc cũng như ý thức kỷ luật. Các phương pháp chấm công truyền thống như ký tên trên giấy hoặc sử dụng thẻ từ vẫn còn tồn tại nhiều hạn chế, tiêu tốn thời gian, dễ xảy ra gian lận và khó quản lý dữ liệu tập trung.

Trước yêu cầu ngày càng cao về tự động hóa và chuyển đổi số, việc ứng dụng công nghệ sinh trắc học – đặc biệt là nhận dạng vân tay – vào hệ thống chấm công là một giải pháp phù hợp, mang lại độ chính xác cao, khó giả mạo và thuận tiện cho người sử dụng. Xuất phát từ thực tiễn đó, nhóm thực hiện đề tài “Hệ thống chấm công bằng vân tay” với mục tiêu xây dựng một mô hình thử nghiệm có tính ứng dụng và khả năng mở rộng trong thực tế.

2. Mục tiêu và phạm vi nghiên cứu
2.1. Mục tiêu nghiên cứu

Đề tài hướng tới các mục tiêu chính sau:

Thiết kế hệ thống chấm công tự động dựa trên công nghệ nhận dạng vân tay.

Kết hợp thêm công nghệ RFID nhằm tăng tính linh hoạt trong hình thức xác thực.

Xây dựng hệ thống ghi nhận và quản lý dữ liệu chấm công theo thời gian thực.

Tích hợp camera giám sát để nâng cao độ tin cậy và minh bạch.

Phát triển phần mềm điều khiển có cấu trúc rõ ràng, dễ bảo trì và mở rộng.

2.2. Phạm vi nghiên cứu

Đề tài tập trung nghiên cứu và triển khai mô hình ở quy mô nhỏ, sử dụng các bo mạch Arduino UNO, ESP32-DEV và ESP32-CAM, phù hợp cho phòng học, văn phòng hoặc cơ quan quy mô vừa và nhỏ. Các nội dung chuyên sâu về an ninh mạng và lưu trữ dữ liệu lớn chưa được đề cập trong phạm vi đề tài.

3. Tổng quan hệ thống và kiến trúc thiết kế

Hệ thống chấm công bằng vân tay được thiết kế theo mô hình phân lớp chức năng, gồm ba khối chính: khối thu thập dữ liệu, khối xử lý trung tâm và khối giám sát hình ảnh.

Khối thu thập dữ liệu sử dụng Arduino UNO kết nối với cảm biến vân tay R307, module RFID RC522 và màn hình LCD I2C. Chức năng chính của khối này là nhận dạng người dùng, hiển thị trạng thái hoạt động và truyền dữ liệu đã xử lý lên khối trung tâm.

Khối xử lý trung tâm sử dụng ESP32-DEV, đảm nhiệm việc xác thực dữ liệu, ghi nhận thời gian chấm công, phát âm thanh thông báo qua DFPlayer Mini, lưu trữ lịch sử chấm công và cung cấp giao diện web để quản lý và theo dõi dữ liệu theo thời gian thực.

Khối giám sát hình ảnh sử dụng ESP32-CAM, hoạt động như một camera IP độc lập, có nhiệm vụ chụp ảnh người dùng tại thời điểm chấm công nhằm hỗ trợ kiểm soát và xác minh thông tin.

4. Nguyên lý hoạt động của hệ thống

Khi hệ thống được cấp nguồn, toàn bộ các thiết bị tiến hành khởi tạo và thiết lập kết nối. Arduino UNO khởi động cảm biến vân tay, module RFID, LCD và kiểm tra trạng thái sẵn sàng của các thiết bị ngoại vi. Đồng thời, ESP32-DEV kết nối mạng WiFi, đồng bộ thời gian thực thông qua NTP và khởi động Web Server cùng WebSocket để phục vụ giao diện quản lý.

Khi người dùng thực hiện chấm công bằng cách đặt vân tay lên cảm biến hoặc quét thẻ RFID, Arduino UNO sẽ thu thập dữ liệu nhận dạng và tiến hành so khớp trực tiếp trên cảm biến vân tay hoặc đọc UID từ thẻ RFID. Kết quả nhận dạng được gửi về ESP32-DEV thông qua giao tiếp Serial.

Tại ESP32-DEV, dữ liệu nhận được được phân tích và xác thực. Nếu hợp lệ, hệ thống ghi nhận thời gian chấm công, phát âm thanh thông báo tương ứng và gửi yêu cầu tới ESP32-CAM để chụp ảnh. Hình ảnh sau khi chụp được truyền về ESP32-DEV thông qua giao thức HTTP, cùng với thông tin người dùng được lưu lại trong lịch sử chấm công và hiển thị trên giao diện web quản lý theo thời gian thực.

5. Thiết kế và thuyết minh phần mềm

Phần mềm hệ thống được thiết kế theo hướng mô-đun, tách biệt rõ ràng chức năng của từng thiết bị.

Chương trình được nạp cho Arduino UNO đảm nhiệm việc giao tiếp trực tiếp với phần cứng, bao gồm đọc vân tay, xử lý đăng ký – xóa – tìm kiếm mẫu vân tay, đọc UID RFID và hiển thị trạng thái lên LCD. Arduino đóng vai trò tiền xử lý dữ liệu, giúp giảm tải cho khối xử lý trung tâm.

Chương trình trên ESP32-DEV đóng vai trò là bộ não của hệ thống. ESP32 nhận dữ liệu từ Arduino, thực hiện các xử lý logic chấm công, điều khiển DFPlayer phát âm thanh thông báo, lưu trữ lịch sử chấm công và quản lý giao diện web. Ngoài ra, ESP32-DEV còn đảm nhiệm giao tiếp với ESP32-CAM để thu thập hình ảnh.

Chương trình trên ESP32-CAM được thiết kế đơn giản nhưng hiệu quả, với nhiệm vụ chính là khởi tạo camera, chụp ảnh khi có yêu cầu và trả dữ liệu ảnh về ESP32-DEV.

6. Kết quả đạt được và đánh giá hệ thống

Qua quá trình triển khai và thử nghiệm, hệ thống hoạt động ổn định, khả năng nhận dạng vân tay nhanh và chính xác. Việc tích hợp camera giúp tăng độ tin cậy của thông tin chấm công và hạn chế hiện tượng gian lận. Giao diện web cho phép quản lý và giám sát trực quan, thuận tiện cho người quản lý.

Tuy nhiên, hệ thống vẫn còn một số hạn chế như chưa tích hợp cơ sở dữ liệu lớn, khả năng bảo mật mạng còn ở mức cơ bản và dung lượng lưu trữ hình ảnh chưa tối ưu.

7. Hướng phát triển và mở rộng

Trong thời gian tới, hệ thống có thể được mở rộng và nâng cấp theo các hướng:

Tích hợp cơ sở dữ liệu tập trung (MySQL, Firebase).

Lưu trữ dữ liệu và hình ảnh trên nền tảng đám mây.

Bổ sung chức năng xuất báo cáo tự động theo ngày, tháng.

Nâng cao tính bảo mật và xác thực người dùng.
