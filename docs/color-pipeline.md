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
