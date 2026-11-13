# Đường xử lý màu sâu (giai đoạn màu chuyên nghiệp, bước 1)

Mục tiêu: video 10-bit/Log và ảnh RAW được chỉnh màu mạnh mà không bị vệt bậc màu (banding).
Bước 1 chỉ làm nền: định dạng khung hình 16-bit float và đi xuyên suốt lõi. Các bước sau
(CST Log → Rec.709, LUT, bánh xe màu, scopes, xuất H.265 Main10, RAW) xây trên nền này.

## Định dạng khung hình

`VideoFrame::format` là `RGBA8` (như trước) hoặc `RGBA16F` (half float, `VideoFrame::half`).
Half float là định dạng texture "sâu" chuẩn trên Metal, D3D11 và GLES 3, nên CPU và GPU dùng chung bố cục.
Chuyển đổi nằm ở `vstudio/pixel.hpp`:

- half ↔ float chính xác (giải mã bằng bảng 64K, mã hóa làm tròn về số chẵn gần nhất);
- RGBA16F → RGBA8 có dither 4×4 có thứ tự, nên gradient mịn không thành dải; giá trị vốn là mức 8-bit thì giữ nguyên.

## Ai dùng định dạng nào

| Chặng | Khi nào sâu (RGBA16F) |
|---|---|
| Giải mã | Khi được yêu cầu (`setDeepOutput`) **và** nguồn > 8 bit. FFmpeg: RGBA64 → half; Apple: `64RGBAHalf` thẳng từ VideoToolbox. Android: chưa (vẫn 8-bit). |
| Hiệu ứng | Clip có hiệu ứng thì giải mã sâu, kể cả khi xem trước. Hiệu ứng có `supportsDeep()` chạy trên float; module cũ chỉ biết 8-bit thì được đưa bản 8-bit (chuyển một lần), không bao giờ nhận half. |
| Ghép lớp | Xem trước: lớp sâu được dither xuống 8-bit **sau** khi chỉnh màu, rồi ghép như cũ (GPU). Xuất sâu: dùng compositor có `supportsDeep()`, hiện là CPU tham chiếu; GPU sẽ thêm sau. |
| Mã hóa | `ExportSettings::bitDepth = 10` và encoder có `supportsDeep()` thì xuất khung sâu; nếu không, xuất 8-bit như cũ. |

## Màu khi giải mã (FFmpeg)

swscale mặc định coi YUV là BT.601. Nay ma trận lấy từ luồng video (BT.709 cho video HD không ghi rõ,
BT.2020 khi có), dải màu (limited/full) theo luồng. Probe ghi thêm `bitDepth`, `transfer` (vd. `arib-std-b67` = HLG),
`primaries`, dùng cho việc tự nhận diện Log/HLG ở bước sau.

## Kiểm thử

- Half: mọi giá trị half hữu hạn khứ hồi chính xác; mọi giá trị 8-bit qua RGBA16F về nguyên vẹn.
- Dither: gradient 10-bit chỉ trải 4 mức 8-bit vẫn giữ được độ dốc (trung bình ô 4×4 lệch < 0,2 mức).
- Chỉnh mạnh một dải tối 10-bit: 8-bit còn ≤ 65 mức, đường sâu giữ > 600 mức.
- FFmpeg: file FFV1 10-bit dải xám 877 mức, giải mã sâu giữ > 700 mức; 8-bit ≤ 256.
- Renderer: compositor không có đường sâu không bao giờ nhận half; hiệu ứng 8-bit không bao giờ nhận half; ảnh sâu và 8-bit khớp nhau.

# Bước 2: Log/HDR → Rec.709 và LUT 3D

Hiệu ứng `builtin.color_space` (`vstudio/color_space.hpp`), luôn đứng đầu chuỗi hiệu ứng của clip:

1. giải mã đường cong Log của máy quay ra ánh sáng tuyến tính (bảng 4097 điểm, nội suy);
2. đổi gam màu máy quay sang Rec.709 (ma trận 3×3 tính từ toạ độ màu cơ bản qua XYZ, D65);
3. phơi sáng, rồi vai mềm cho vùng sáng (xám 18% giữ nguyên, vùng sáng cuộn dần thay vì cháy);
4. mã hoá Rec.709.

| id | Định dạng | Gam màu |
|---|---|---|
| `slog3` | Sony S-Log3 | S-Gamut3.Cine |
| `vlog` | Panasonic V-Log | V-Gamut |
| `clog3` | Canon Log 3 | Cinema Gamut |
| `nlog` | Nikon N-Log | Rec.2020 |
| `applelog` | Apple Log (iPhone 15 Pro trở lên) | Rec.2020 |
| `dlog` | DJI D-Log | D-Gamut |
| `hlg` | HLG (HDR của điện thoại) | Rec.2020 |

Công thức theo tài liệu công bố của từng hãng. Test kiểm tra mỗi định dạng: xám 18% của hãng ra đúng xám giữa Rec.709 (0,409), màu xám giữ trung tính, vùng sáng không cháy, kết quả 8-bit và 16-bit khớp nhau.
**DJI D-Log M** không có công thức công bố: dùng LUT chính thức của DJI qua `builtin.lut`.
Camera Log hiếm khi được ghi trong file nên người dùng chọn; HLG thì file có ghi, app gợi ý ngay.

`builtin.lut`: LUT 3D `.cube` (Resolve/Adobe; có `DOMAIN_MIN/MAX`), nội suy ba chiều tuyến tính, có độ mạnh.
LUT được kiểm tra khi chọn (file hỏng bị từ chối ngay), lưu đường dẫn tương đối theo dự án, và đi kèm trong gói `.vstudiopkg`.
Trên mobile, LUT được chép vào thư mục media của app như video.
