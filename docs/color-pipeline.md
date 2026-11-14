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

# Bước 3: Chỉnh màu chuyên nghiệp (bánh xe màu, đường cong, HSL, chọn vùng màu)

Hiệu ứng `builtin.color_grade` (`vstudio/grade.hpp`). Mỗi clip có **một** hiệu ứng chỉnh màu chứa toàn bộ bản chỉnh,
nên một lần chỉnh là một bước hoàn tác, xem trước và xuất ra giống hệt nhau, và chỉ là dữ liệu JSON trong dự án
(file gốc không bao giờ bị sửa). Vị trí trong chuỗi hiệu ứng: sau chuyển Log → Rec.709 và LUT máy quay, trước bộ lọc
một chạm. Ảnh tĩnh dùng cùng hiệu ứng.

Thứ tự xử lý trong hiệu ứng:

1. Bánh xe màu: Offset (tổng thể), Lift (vùng tối), Gamma (trung tính), Gain (vùng sáng). Mỗi bánh xe có
   cân bằng màu (kéo về phía màu muốn đẩy; không đổi độ sáng) và vòng/thanh độ sáng riêng.
2. Tương phản quanh điểm tựa (mặc định 0,435 = xám giữa Rec.709).
3. Đường cong: Tổng, rồi Đỏ / Lục / Lam. Đường cong đơn điệu (Fritsch–Carlson) nên không vọt quá giữa các điểm;
   desktop và mobile vẽ đúng đường cong được áp dụng (`sampleCurve`).
4. Bão hoà.
5. HSL: sắc độ / bão hoà / độ sáng cho 8 dải màu (Đỏ, Cam, Vàng, Xanh lá, Lơ, Xanh dương, Tím, Hồng tím);
   các dải hoà mượt vào nhau, màu xám không bị ảnh hưởng.
6. Chọn vùng màu (qualifier): chọn theo màu + độ rộng, khoảng bão hoà, khoảng độ sáng, biên mềm; có thể đảo.
   Chỉnh riêng vùng đó: sắc độ, bão hoà, độ sáng. "Lấy màu" chọn nhanh bằng một cú nhấp/chạm trên khung hình;
   "Hiện vùng chọn" cho xem vùng được chọn (trắng) — chỉ để xem, không lưu vào dự án.

Bước 1–3 tính chung một bảng tra cho mỗi khung (256 mục cho 8-bit, 4097 mục nội suy cho khung 16-bit), bước 5–6 dùng bảng
theo sắc độ, nên chi phí mỗi điểm ảnh nhỏ. Khung lớn (xuất 4K) được chia hàng cho các nhân CPU.

## Xem trước trực tiếp, không làm đầy lịch sử hoàn tác

Khi đang kéo bánh xe, điểm đường cong hay thanh trượt, giao diện gửi tham số tạm qua `PreviewEngine::setEffectOverride`
(C API `vs_set_effect_preview`): bộ xem trước chỉ thay chuỗi hiệu ứng của **clip đó**, dự án và lịch sử hoàn tác không đổi.
Khung nguồn đã giải mã vẫn nằm trong cache, nên mỗi lần kéo chỉ tốn phần chỉnh màu + ghép lớp. Nhả chuột/nhấc tay mới lưu
một bước hoàn tác (`Editor::setClipGrade`, `vs_set_clip_grade`). Mọi chỉnh sửa khác hoặc hoàn tác đều xoá bản tạm.
Thông số về trung tính thì hiệu ứng tự bị gỡ khỏi clip.

## Giao diện

- Desktop: bảng "Chỉnh màu" (cạnh "Bộ lọc màu"): 4 bánh xe + tương phản/điểm tựa/bão hoà; trình sửa đường cong
  (nhấp thêm điểm, kéo, nhấp đúp để xoá); HSL; Vùng màu với nút "Lấy màu…" (nhấp lên khung xem trước, Esc để huỷ).
  "Xem gốc" (giữ chuột) so sánh với clip chưa chỉnh; "Áp dụng cho tất cả" chép bản chỉnh cho mọi clip (một bước hoàn tác).
- Mobile: công cụ "Chỉnh màu" mở bảng ngay dưới khung xem trước (hình vẫn thấy): bánh xe từng cái một, đường cong
  (chạm thêm điểm, giữ lâu để xoá), HSL, Vùng màu (bấm "Lấy màu" rồi chạm vào hình). "Giữ: xem gốc" để so sánh.

## Kiểm thử và số đo

- Thông số trung tính không đổi ảnh (8-bit và 16-bit); lift nâng vùng tối, gain hạ vùng sáng, gamma nâng trung tính,
  đầu mút giữ nguyên; cân bằng màu không đổi độ sáng; bánh xe ↔ cân bằng khứ hồi chính xác.
- Đường cong qua đúng điểm, không vọt; đường cong Lam chỉ đổi kênh lam.
- HSL: bỏ bão hoà dải Xanh dương làm trời xám đi, màu đỏ và màu xám giữ nguyên; các dải hoà vào nhau.
- Chọn vùng màu: chỉ vùng trời đổi, chủ thể đỏ giữ nguyên; mặt nạ trắng/đen đúng; đảo; biên mềm.
- 8-bit và 16-bit khớp nhau (lệch ≤ 3 mức); dải tối 10-bit đẩy mạnh vẫn giữ > 600 mức.
- Editor: một hiệu ứng mỗi clip, đúng vị trí (sau Log, trước bộ lọc), giữ id khi sửa, hoàn tác từng bước, `show_mask` không bị lưu.
- Xem trước trực tiếp đổi hình mà dự án không đổi; clip khác không bị ảnh hưởng.
- Tốc độ CPU (máy ảo CI 4 nhân): chỉnh màu đầy đủ khung xem trước 960×540 ≈ 6 ms; khung 4K 16-bit ≈ 0,4 s.
  Vì vậy khi có GPU, phần chỉnh màu chạy trên GPU (mục dưới).

## Chỉnh màu trên GPU

Compositor Metal (macOS/iOS), Direct3D 11 (Windows) và OpenGL ES (Android) chạy chỉnh màu ngay trong shader ghép lớp
(`ICompositor::supportsGrade()`). Khi hiệu ứng cuối cùng của clip là `builtin.color_grade`, `Renderer` không chạy nó
trên CPU mà gắn `GpuGrade` vào lớp (`CompositeLayer::grade`) và giữ khung 16-bit: lớp được tải lên GPU dạng RGBA16F,
chỉnh màu trong float rồi mới ghép và lượng tử về 8-bit. GPU dùng **cùng bảng tra** với CPU
(`gpuGrade()`: 1 texture RGBA32F 4098×2 — hàng 0 là đường cong tông màu, hàng 1 là HSL + mặt nạ sắc độ theo độ),
tạo một lần cho mỗi bộ thông số và được cache, nên kéo bánh xe chỉ đổi vài số và một texture nhỏ.
Compositor CPU và cảnh chuyển vẫn chỉnh trên CPU (cùng kết quả). Kiểm thử chung cho mọi compositor
("colour grade in the compositor matches the CPU grade") so 5 bản chỉnh trên lớp 8-bit và 10-bit với kết quả CPU.

# Bước 4: Xuất H.265 10-bit

Hộp thoại xuất (desktop) và bảng xuất (mobile) có thêm lựa chọn **định dạng**, dùng chung danh sách
`exportFormats()` (`vstudio/export_presets.hpp`, C API `vs_export_formats_json`, `vs_export_start_preset_format`):

| Định dạng | Codec | Bitrate so với H.264 | Dùng khi |
|---|---|---|---|
| H.264 | H.264 High, 8-bit | 1× | Phát ở mọi nơi |
| H.265 (HEVC) | H.265 Main, 8-bit | ≈ 0,65× | File nhỏ hơn, máy đời mới |
| H.265 10-bit | H.265 Main 10 | ≈ 0,75× | Video Log/10-bit đã chỉnh màu: không vệt bậc màu |

Với H.265 10-bit, `exportProject` dựng hình ở 16-bit float (`setDeepOutput`) và bộ mã hoá nhận thẳng khung
RGBA16F, đổi sang YCbCr Rec.709 giới hạn 10-bit (gắn thẻ màu BT.709 trong file). Bộ mã hoá theo nền tảng,
ưu tiên phần cứng (GPU / media engine):

- macOS / iOS: AVFoundation + VideoToolbox (`AVVideoCodecTypeHEVC`, `HEVC_Main10_AutoLevel`, bộ đệm 10-bit 4:2:0).
- Windows / Linux (FFmpeg): `hevc_videotoolbox`, `hevc_nvenc` (NVIDIA), `hevc_qsv` (Intel), `hevc_amf` (AMD),
  `hevc_mf` (Media Foundation), cuối cùng `libx265` nếu bản FFmpeg có. File MP4 gắn `hvc1` để QuickTime/iPhone phát được.
- Android: MediaCodec `video/hevc`, Main 10 với đầu vào P010 (Android 13+).

Máy không có bộ mã hoá cho định dạng đã chọn thì việc xuất dừng ngay với thông báo rõ ràng; không bao giờ lặng lẽ
xuất ra 8-bit hay H.264. Kiểm thử: dải xám 1024 cột qua H.265 10-bit còn ≈ 870 mức (8-bit tối đa 256); clip 10-bit
có chỉnh màu xuất ra còn ≈ 770 mức. Bộ kiểm thử chung chạy trên cả FFmpeg, AVFoundation và MediaCodec.

# Bước 5: Ảnh RAW từ máy ảnh

Ảnh RAW (DNG, CR2/CR3, NEF, ARW, RAF, ORF, RW2, PEF, …) nhập như mọi ảnh khác (desktop: hộp "Nhập media" có
mục "Ảnh RAW"; mobile: "Tệp"). Thiết kế theo module, lõi không gọi thư viện RAW nào:

- Hợp đồng `IStillDecoder` (loại module `media.still`, `vstudio/stills.hpp`): `probe` (chỉ đọc đầu file) và
  `decode(path, maxW, maxH, deep)`.
- `withStillDecoders(backend, modules)` bọc backend media của phiên: file RAW đi tới module RAW có ưu tiên cao nhất
  đọc được nó (thử lần lượt), mọi file khác vẫn đi tới FFmpeg / AVFoundation / MediaCodec. Module RAW chỉ được nạp
  khi mở file RAW đầu tiên.
- `libraw` (mọi hệ điều hành): LibRaw 0.21 (LGPL/CDDL). Dùng thư viện hệ thống nếu có (Linux, Homebrew), nếu không
  thì tự biên dịch từ mã nguồn (Windows, Android, iOS), không thêm phụ thuộc.
- `apple.raw` (macOS 12+/iOS 15+, ưu tiên cao hơn): CIRAWFilter của Apple, hồ sơ máy ảnh mới nhất, rửa ảnh trên GPU.
  File nó không đọc được (hoặc máy cũ) tự chuyển sang LibRaw.

Rửa ảnh: cân bằng trắng của máy, ma trận màu máy → gam Rec.709, đường truyền Rec.709, 16 bit mỗi kênh. Khi có chỉnh
màu hoặc xuất 10-bit, ảnh vào đường xử lý dạng RGBA16F nên giữ đủ 12–14 bit của cảm biến. Xem trước xin đúng kích
thước khung xem: LibRaw rửa ở nửa độ phân giải (không cần demosaic, nhanh ≈ 4 lần), Apple dùng `scaleFactor` và chế
độ nháp; xuất dùng đủ kích thước. Ảnh rửa một lần rồi dùng lại cho mọi thời điểm của clip.

Kiểm thử chung `core/tests/still_conformance.hpp` (chạy cho cả LibRaw và Apple) dùng một file DNG 14-bit tự tạo
(`raw_sample.hpp`): đọc kích thước, dải xám còn > 400 mức (≈ 1000 với LibRaw; 8-bit tối đa 256), xám trung tính,
khung xem trước vừa hộp, ảnh dọc được xoay đúng, định tuyến qua backend (file RAW không bao giờ tới backend video),
nhập vào dự án là ảnh. Desktop có thêm phép thử nhập DNG qua giao diện.
