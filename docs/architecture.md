# Kiến trúc Video Studio (P0)

Tài liệu này mô tả cách mã nguồn hiện thực đề án hợp nhất v2.0 và hai nguyên tắc chủ sở hữu dự án đặt ra
cho code:

1. **Mượt, không giật lag**, kể cả với video dung lượng lớn và nhiều clip ghép.
2. **Mở theo module**: lõi nhỏ, tính năng là module cắm vào; module chỉ được gọi khi cần, nhận dữ liệu và trả kết quả.

## 1. Sơ đồ lớp

```
 ┌────────────────────────────┐   ┌───────────────────────────────┐
 │ Desktop: Qt 6 (QML)        │   │ Mobile: Flutter (Dart)        │
 │ desktop/                   │   │ mobile/  ── dart:ffi ──┐      │
 └──────────────┬─────────────┘   └────────────────────────┼──────┘
                │ C++ trực tiếp                            │
                │                        ┌─────────────────▼──────┐
                │                        │ C API ổn định (capi/)  │
                │                        └─────────────────┬──────┘
 ┌──────────────▼──────────────────────────────────────────▼──────┐
 │ Video Studio Core (core/) – C++20, không UI, không API hệ điều  │
 │ hành, không mạng                                                │
 │  Editor ─ CommandHistory ─ Timeline model ─ schema .vstudio v0  │
 │  PreviewEngine ─ Renderer/Compositor ─ FrameCache ─ Proxy       │
 │  JobScheduler ─ ModuleRegistry (hợp đồng interface)             │
 └──────────────▲──────────────────────────▲──────────────────────┘
                │ đăng ký module           │
 ┌──────────────┴───────────┐  ┌───────────┴────────────────────────┐
 │ adapters/ffmpeg (đã có)  │  │ adapters/apple, adapters/android   │
 │ media.backend            │  │ (P0 tuần 3–4, cùng interface)      │
 └──────────────────────────┘  └────────────────────────────────────┘
```

## 2. Chỉnh sửa chỉ là dữ liệu (không đụng media)

- Mỗi thao tác cắt, tách, kéo, trim là một `EditCommand` trên mô hình Timeline trong RAM
  (`core/src/timeline.cpp`, `core/src/history.cpp`). Lệnh chỉ đổi điểm vào/ra, vị trí; không đọc, không ghi file video.
- Undo/redo lưu ảnh chụp của đúng các track bị sửa, nên hoàn tác luôn khôi phục chính xác trạng thái cũ.
- Sau mỗi lệnh, `Editor::snapshot()` phát hành một bản sao **bất biến** của project cho các luồng render.
  Luồng giao diện không bao giờ chờ luồng render và ngược lại.
- Thời gian dùng số nguyên "tick" (1/705.600.000 giây), chia hết cho mọi tốc độ khung hình và tần số lấy mẫu phổ
  biến (23,976 / 25 / 29,97 / 30 / 60 fps; 44,1 / 48 kHz), nên không có sai số tích lũy và không lệch tiếng-hình.
- Media chỉ được giải mã ở hai nơi: preview (luồng nền) và export (job nền), đúng hướng "có lệnh mới xử lý".

Số đo (`docs/benchmarks/p0-linux-4core.md`): với timeline 3.000 clip, mỗi thao tác tách/kéo/trim mất khoảng 1–1,5 ms,
undo khoảng 0,2 ms, nằm gọn trong một khung hình 16 ms.

## 3. Luồng xử lý và chống giật

| Luồng | Làm gì | Không bao giờ làm |
|---|---|---|
| UI (Qt/Flutter) | vẽ, nhận thao tác, gọi lệnh chỉnh sửa | giải mã, đọc file media, chờ I/O |
| Preview video | render khung hình mới nhất được yêu cầu, đọc trước vài khung khi đang phát | xếp hàng các yêu cầu cũ |
| Preview audio | giữ bộ đệm vòng ~250 ms | để thiết bị âm thanh phải chờ giải mã |
| Job Scheduler | probe, proxy, export, (sau này) thumbnail, waveform | chiếm hết CPU: chừa 1 lõi cho UI |

Các kỹ thuật chính:

- **Yêu cầu mới nhất thắng** (`PreviewEngine::requestFrame`): kéo playhead nhanh qua timeline dài chỉ render khung
  cuối cùng, không tồn đọng.
- **Giải mã thẳng về kích thước preview** (`IMediaDecoder::setMaxOutputSize`): video 4K không bao giờ bị bung thành
  khung RGBA 4K khi preview. Đo được: 4K giải mã + chuyển đổi khoảng 4,8 ms/khung thay vì 41 ms.
- **Cache theo khung nguồn** (`FrameCache`): khóa là (asset, khung nguồn, kích thước), không phải vị trí trên timeline,
  nên sau khi cắt/kéo clip, preview dựng lại từ cache mà không giải mã lại. Đo được: 0,7 ms/khung sau thao tác
  tách + kéo.
- **Proxy cho media lớn** (`core/src/proxy.cpp`): video vượt 1080p hoặc VFR được chuyển nền thành bản 540p, GOP 10.
  Tua ngẫu nhiên trên 4K GOP 60 giảm từ khoảng 115 ms xuống khoảng 10 ms. Export luôn đọc file gốc.
- **Đồng hồ âm thanh là đồng hồ chính**: playhead tính từ số mẫu âm thanh đã phát; hình bám theo tiếng.
- **Export tính mẫu âm thanh theo ranh giới khung**: tổng số mẫu luôn khớp chính xác độ dài video (có test).
- **Kéo clip trên Timeline chỉ là hình vẽ** cho đến khi thả tay: một lệnh, một bước undo.

## 4. Module: lõi nhỏ, chức năng cắm vào

- `ModuleRegistry` (`core/include/vstudio/modules.hpp`) giữ danh sách module: id, loại, phiên bản, khả năng,
  nền tảng, độ ưu tiên và một "nhà máy" tạo module.
- Module **chỉ được khởi tạo ở lần gọi đầu tiên** và dùng lại sau đó. Lõi không biết bên trong module.
- Lỗi khi khởi tạo hoặc khi chạy module được trả về như một lỗi của lệnh đó, không làm sập ứng dụng.
  Hiệu ứng không có module trên máy đang mở vẫn được giữ nguyên trong project (cảnh báo "chỉ xem").
- Các loại module hiện có:
  - `media.backend` (`IMediaBackend`): probe, decode, encode. Đã có: FFmpeg. Sắp có: Apple, MediaCodec.
  - `effect.video` (`IVideoEffect`): khung hình vào, khung hình ra. Đã có: `builtin.color_adjust`, `builtin.grayscale`.
- Thêm một module mới = viết một lớp theo interface + một dòng `registry.add<...>(info, factory)`. Không sửa lõi.
- Từ V2.0, Plugin API nạp module từ `.dll/.so/.dylib` qua C API ổn định (`capi/`).

## 5. Định dạng dự án `.vstudio` (schema v0)

```
MyFilm.vstudio/
  project.json            manifest + timeline, có schema_version và timebase
  project.json.bak        bản lưu trước
  recovery/autosave.json  tự lưu định kỳ, ghi kiểu atomic (file tạm rồi đổi tên)
  cache/proxy/<asset>.mp4 proxy (có thể xóa, sẽ tạo lại)
```

- Đường dẫn media lưu tương đối theo thư mục chứa project khi có thể; `asset_id` + `fingerprint` dùng để relink.
- Trường không biết được giữ nguyên khi đọc rồi ghi lại (round-trip), đúng nguyên tắc 4.3 của đề án.
- Project có schema mới hơn bản đang chạy bị từ chối mở, không đọc sai.

## 6. Thư mục mã nguồn

| Thư mục | Nội dung |
|---|---|
| `core/` | Video Studio Core + test (`core/tests`) |
| `adapters/ffmpeg/` | module FFmpeg + test tích hợp với file thật |
| `capi/` | C API cho Flutter và plugin + test bằng C thuần |
| `desktop/` | ứng dụng Qt 6 (QML) |
| `mobile/` | ứng dụng Flutter |
| `tools/vstudio-cli/` | công cụ dòng lệnh chạy luồng P0 không cần giao diện |
| `bench/` | benchmark P0 |
| `design/tokens.json` | design token dùng chung cho Qt và Flutter |
