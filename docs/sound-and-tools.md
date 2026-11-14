# Âm thanh, công cụ clip, yêu thích, tablet và plugin

Các phần còn lại của đề án v2.0 được hoàn thiện trong một đợt: FR-011, FR-015, FR-020, FR-022, FR-025..FR-029.

## Âm thanh (`core/include/vstudio/audio_fx.hpp`)

| Chức năng | Desktop | Mobile | Lưu trong project |
|---|---|---|---|
| Âm lượng, hiện dần / tắt dần (≤ 10 s, đường cong sin²) | panel Âm thanh | sheet Âm thanh | `clip.volume`, `audio.fade_in/fade_out` |
| EQ 5 dải (80 Hz…10 kHz) + 10 preset | panel Âm thanh (thanh trượt từng dải) | chip preset | `audio.eq` |
| Khử tiếng ồn nền (đo nền, rồi trừ phổ) | panel Âm thanh | sheet Âm thanh | `audio.denoise` (mức + hồ sơ ồn) |
| Tách âm thanh ra track riêng | nút "Tách âm thanh" | nút "Tách âm thanh" | `audio.detached` + clip trên track audio |
| Mixer: âm lượng, pan, mute, solo từng track | dock Mixer | cuối sheet Âm thanh | `track.pan`, `track.solo` |
| Thu âm (voice-over) tại playhead | nút "Ghi âm" (QMediaRecorder) | nút "Thu âm" (gói `record`) | clip trên track "Thu âm" |

- Đo tiếng ồn chạy nền, có tiến độ và hủy; kết quả áp vào project như một lệnh (một bước undo).
- Hiệu ứng tiếng là module `effect.audio`, được cấp thêm mẫu nguồn hai đầu khối, nên preview (khối nhỏ) và export
  (khối lớn) cho cùng một âm thanh, không có tiếng tách ở mép khối.

## Công cụ clip (`core/include/vstudio/clip_tools.hpp`)

- **Phát ngược** (`clip.reverse`): hình và tiếng. Hình được giải mã xuôi theo khối vào cache rồi phát ngược, nên kéo
  playhead không bị giật; export dùng cache riêng 384 MB.
- **Đóng băng khung** tại playhead: chèn một clip giữ hình 0,5–10 s, phần sau của clip lùi lại.
- **Tách nền màu (chroma key)**: màu nền (xanh lá, xanh dương, hoặc tự đoán từ preview), độ tương đồng, làm mềm viền, khử ám màu.
- **Chống rung**: đo độ rung nền một lần; đổi độ mạnh chỉ làm mượt lại đường đã đo, không đo lại. Tự phóng to tối đa 1,25× để che mép.

## Yêu thích (FR-022)

Đánh sao Look, chuyển cảnh, hiệu ứng, sticker, overlay (desktop: chuột phải; mobile: nhấn giữ). Mục đã đánh sao
lên đầu danh sách. Lưu trong `favorites.json` ở thư mục dữ liệu người dùng (ghi atomic).

## Tablet và điện thoại gập (FR-027)

`mobile/lib/ui/workspace_layout.dart` chọn bố cục từ kích thước màn hình và bản lề:

- điện thoại dọc: preview trên, công cụ dưới;
- tablet ngang (≥ 840 dp) hoặc điện thoại ngang: preview trái, công cụ phải;
- máy gập mở dạng sách (Galaxy Z Fold, iPhone gập): mỗi nửa màn hình một phần, chừa khoảng bản lề;
- gập nửa đặt trên bàn: preview ở nửa trên, công cụ ở nửa dưới.

## Plugin API (beta, FR-029)

Xem [plugins/README.md](../plugins/README.md). Lõi: `core/include/vstudio/plugins.hpp`.
