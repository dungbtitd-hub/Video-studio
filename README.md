# Video Studio

Trình dựng video offline cho Windows, macOS, Android và iOS: một lõi C++20 dùng chung, giao diện Qt 6 cho
desktop và Flutter cho mobile (đề án hợp nhất v2.0).

Hai nguyên tắc của mã nguồn:

- **Mượt, không giật lag**: chỉnh sửa chỉ thay đổi dữ liệu Timeline, mọi việc nặng (giải mã, proxy, export) chạy nền.
- **Mở theo module**: lõi chỉ giữ interface; mỗi chức năng là một module đăng ký vào `ModuleRegistry`, được tạo
  khi cần, nhận dữ liệu và trả kết quả.

| Tài liệu | |
|---|---|
| [docs/architecture.md](docs/architecture.md) | kiến trúc, luồng xử lý, module, định dạng `.vstudio` |
| [docs/p0-status.md](docs/p0-status.md) | tiến độ P0 và cách build/chạy |
| [docs/sound-and-tools.md](docs/sound-and-tools.md) | âm thanh, mixer, thu âm, phát ngược, đóng băng khung, chroma key, chống rung, yêu thích, tablet |
| [plugins/README.md](plugins/README.md) | Plugin API (beta): viết hiệu ứng ngoài |
| [docs/benchmarks/](docs/benchmarks/) | số đo hiệu năng |
| [mobile/README.md](mobile/README.md) | ứng dụng Flutter |
| [packaging/README.md](packaging/README.md) | đóng gói bản cài cho Windows, macOS, Linux, Android, iOS |

## Build nhanh

```bash
cmake -S . -B build -G Ninja -DVSTUDIO_BUILD_DESKTOP=ON
cmake --build build
ctest --test-dir build --output-on-failure
```
