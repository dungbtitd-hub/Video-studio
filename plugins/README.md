# Plugin API (beta)

Plugin thêm hiệu ứng hình và hiệu ứng tiếng cho Video Studio **desktop** (Windows, macOS, Linux) mà không cần
sửa hay build lại ứng dụng. iOS và Android không nạp plugin (quy định của kho ứng dụng); project dùng hiệu ứng
plugin vẫn mở được ở đó, hiệu ứng được giữ nguyên và chỉ không hiện ra.

## Cấu trúc một plugin

```
my-plugin/
  plugin.json
  libmyfx.so | libmyfx.dylib | myfx.dll
```

```json
{
  "abi": 1,
  "id": "com.example.fx",
  "name": "Hiệu ứng của tôi",
  "version": "1.0.0",
  "library": {"windows": "myfx.dll", "macos": "libmyfx.dylib", "linux": "libmyfx.so"},
  "video_effects": [{"id": "com.example.fx.invert", "name": "Âm bản", "params": {"amount": 1.0}}],
  "audio_effects": [{"id": "com.example.fx.gain", "name": "Giảm âm lượng", "params": {"gain": 0.5}}]
}
```

- Thư viện xuất đúng một hàm `vs_plugin_entry` theo header C [`capi/include/vstudio/vstudio_plugin.h`](../capi/include/vstudio/vstudio_plugin.h).
- `id` của hiệu ứng là duy nhất; tiền tố `builtin.` dành cho ứng dụng.
- `params` là tham số mặc định khi người dùng bật hiệu ứng; chúng được lưu trong project và gửi cho plugin dưới dạng chuỗi JSON.

## Ứng dụng nạp plugin thế nào

1. Khi khởi động, ứng dụng chỉ đọc các `plugin.json` trong:
   - `<thư mục ứng dụng>/plugins` (Windows, Linux) hoặc `VideoStudio.app/Contents/PlugIns` (macOS),
   - thư mục dữ liệu người dùng: `%APPDATA%/VideoStudio/plugins`, `~/Library/Application Support/VideoStudio/plugins`, `~/.local/share/VideoStudio/plugins`,
   - các thư mục trong biến môi trường `VSTUDIO_PLUGIN_PATH`.
2. Thư viện **chỉ được mở lần đầu một hiệu ứng của nó được dùng**. Project không dùng plugin thì không tốn gì.
3. Manifest hỏng, sai phiên bản ABI, thiếu thư viện hay thiếu hàm: plugin bị bỏ qua và báo lỗi trong panel
   "Công cụ clip", ứng dụng không bị sập.

Người dùng bật hiệu ứng plugin trong panel **Công cụ clip → Hiệu ứng plugin (beta)**. Mỗi lần bật/tắt là một bước undo.

## Quy tắc khi viết plugin

- Hiệu ứng chạy trên các luồng render, song song: hàm phải **an toàn đa luồng** và **không giữ trạng thái** giữa
  các lần gọi (mọi thứ cần thiết nằm trong `params_json`).
- Hiệu ứng hình nhận ảnh RGBA 8-bit, sửa tại chỗ. Hiệu ứng tiếng nhận mẫu float xen kẽ các kênh, sửa tại chỗ;
  bộ lọc có "trí nhớ" khai báo `margin` (số mẫu nguồn cần thêm ở hai đầu) để kết quả giống nhau dù preview hay export
  cắt khối khác nhau.
- Trả về 0 nếu thành công. Không gọi lại vào ứng dụng, không mở cửa sổ.
- Plugin chạy trong tiến trình của ứng dụng: chỉ cài plugin bạn tin cậy.

## Ví dụ

[`example/`](example/) là plugin mẫu (đảo màu có mức độ + giảm âm lượng), được build cùng project
(`vstudio_example_plugin`, ra `<build>/plugins/example`). Thử với ứng dụng:

```bash
VSTUDIO_PLUGIN_PATH=build/plugins ./build/bin/VideoStudio
```
