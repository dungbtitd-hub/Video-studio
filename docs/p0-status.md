# Tiến độ P0 – Prototype chung

Đối chiếu với đề án v2.0, mục 10 (lộ trình) và 10.2 (kế hoạch 90 ngày).

| Hạng mục P0 | Trạng thái | Ghi chú |
|---|---|---|
| Timebase chính xác | Xong | tick nguyên 1/705.600.000 s, test 29,97/23,976 fps, 44,1/48 kHz |
| Timeline model, command, undo/redo | Xong | tách, trim, kéo, xóa, tốc độ; lệnh lỗi không làm hỏng project |
| Schema project v0 (.vstudio) | Xong | round-trip giữ trường lạ, ghi atomic, autosave + phục hồi |
| Interface media (decoder/encoder/audio/file/GPU/thermal) | Xong | `core/include/vstudio/media.hpp` |
| Module Registry | Xong | tạo khi cần, chọn theo khả năng/nền tảng, cô lập lỗi |
| Render graph tối thiểu | Xong (CPU) | compositor CPU + hiệu ứng qua module; backend GPU sau P0 |
| Preview nền + đồng bộ A/V | Xong | luồng video, luồng audio, đồng hồ theo mẫu âm thanh |
| Cache khung hình + proxy | Xong | xem `docs/benchmarks/` |
| Export MP4 H.264/AAC | Xong (FFmpeg) | chạy nền, có tiến độ và hủy |
| Desktop Qt shell | Xong (QML và Widgets) | nhập, preview, phát, tách, kéo, trim, undo, lưu/mở, export |
| Desktop Qt Widgets prototype | Xong, chờ chốt | so sánh trong `docs/benchmarks/qml-vs-widgets.md`; đề xuất chọn Widgets, chờ đo trên máy Windows/macOS thật |
| Module FFmpeg (Windows) | Code xong | CI Windows hiện build core không FFmpeg; cần thêm FFmpeg qua vcpkg |
| Module Apple (macOS/iOS) | Chưa | AVFoundation/VideoToolbox, dùng chung macOS và iOS |
| Module MediaCodec (Android) | Đang làm | nhánh `p0/android-mediacodec`, chưa build |
| Flutter shell + FFI | Xong (chạy trên Linux) | FFI đã test end-to-end với core thật; chưa chạy trên điện thoại |
| Preview mobile qua native texture | Chưa | hiện copy khung RGBA qua FFI |
| CI 4 hệ điều hành | Xong (cấu hình) | Linux, macOS, Windows, Android, iOS, Flutter |
| Benchmark trên máy/thiết bị thật | Chưa | mới có số đo trên máy ảo Linux |

## Cách chạy nhanh

```bash
# Linux/macOS: cần CMake ≥ 3.21, Ninja, FFmpeg dev, (tùy chọn) Qt 6.4+
cmake -S . -B build -G Ninja -DVSTUDIO_BUILD_DESKTOP=ON
cmake --build build
ctest --test-dir build --output-on-failure

./build/tools/vstudio-cli/vstudio-cli demo out.mp4 clip1.mp4 clip2.mp4   # luồng P0 không giao diện
./build/bin/VideoStudio clip1.mp4 clip2.mp4                              # desktop bản QML
./build/bin/VideoStudioWidgets clip1.mp4 clip2.mp4                       # desktop bản Widgets
./build/bin/VideoStudioWidgets --bench-timeline 3000                     # đo độ mượt timeline (cả bản QML)
./build/bench/vstudio_bench                                              # benchmark
```
