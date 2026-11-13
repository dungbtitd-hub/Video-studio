# Desktop: Qt Quick (QML) hay Qt Widgets?

Đề án v2.0 yêu cầu làm thử cả hai trước khi chốt công nghệ giao diện desktop.
Hai bản dùng chung đúng một lớp điều khiển (`desktop/common/EditorController`)
và cùng core, nên khác biệt đo được chỉ đến từ lớp giao diện.

| | Bản QML | Bản Widgets |
|---|---|---|
| Chạy | `build/bin/VideoStudio` | `build/bin/VideoStudioWidgets` |
| Timeline | Mỗi clip là một delegate QML (Repeater) | Một widget tự vẽ, chỉ vẽ clip đang hiện (tìm nhị phân theo track) |
| Dữ liệu cho timeline | Model QVariant dựng từ snapshot | Đọc thẳng snapshot bất biến của core |
| Kéo/trim clip | Di chuyển delegate, ghi lệnh khi thả | Chỉ vẽ lại, ghi đúng 1 lệnh khi thả (1 bước undo) |
| Bố cục | Cố định | Dock kéo thả, tách cửa sổ, ẩn/hiện panel |

## Số đo

Máy ảo Linux 4 nhân (Xeon 2,1 GHz), Qt 6.4.2, Xvfb, render phần mềm, bản RelWithDebInfo.
Project giả lập: 3 track, clip dài 2–6 s. Chạy lại bằng:

```bash
xvfb-run -a ./build/bin/VideoStudioWidgets --bench-timeline 3000
xvfb-run -a ./build/bin/VideoStudio        --bench-timeline 3000
```

Mỗi bản cuộn timeline 300 bước (mỗi bước 7 s), rồi kéo 50 clip ra cuối timeline.
Widgets đo đến khi timeline vẽ xong và đẩy lên cửa sổ; QML đo đến khi cửa sổ
đổi khung hình (`frameSwapped`).

| Thao tác | Số clip | QML trung bình / p95 | Widgets trung bình / p95 |
|---|---:|---:|---:|
| Cuộn timeline | 300 | 8,2 / 10,6 ms | 0,85 / 1,4 ms |
| Kéo 1 clip (ghi lệnh + vẽ lại) | 300 | 86 / 143 ms | 1,5 / 2,0 ms |
| Cuộn timeline | 3000 | 31,5 / 40,3 ms | 1,3 / 1,7 ms |
| Kéo 1 clip (ghi lệnh + vẽ lại) | 3000 | 911 / 1381 ms | 4,9 / 6,3 ms |

Windows Server 2022 (máy runner GitHub, không GPU, Qt 6.8.3, bản Release, CI job
`desktop-bench`):

| Thao tác | Số clip | QML trung bình / p95 | Widgets trung bình / p95 |
|---|---:|---:|---:|
| Cuộn timeline | 300 | 16,4 / 16,5 ms (*) | 0,39 / 0,82 ms |
| Kéo 1 clip (ghi lệnh + vẽ lại) | 300 | 127 / 147 ms | 1,3 / 1,8 ms |
| Cuộn timeline | 3000 | 73,5 / 76,3 ms | 0,83 / 1,2 ms |
| Kéo 1 clip (ghi lệnh + vẽ lại) | 3000 | 1115 / 1164 ms | 4,9 / 6,9 ms |

(*) Trên Windows, QML chờ vsync nên mỗi khung tối thiểu khoảng 16,7 ms; con số này
nghĩa là theo kịp 60 fps. Với 3000 clip QML chỉ còn khoảng 13 fps.

Mốc mượt là 16,7 ms mỗi khung (60 fps). Với 3000 clip, bản QML cuộn chỉ còn
khoảng 30 fps và mỗi lần kéo clip đứng hình gần 1 giây. Bản Widgets ở dưới mốc
trong mọi trường hợp; trong 4,9 ms của một lần kéo clip, phần lớn là core
(lệnh sửa khoảng 1,5 ms, snapshot khoảng 1,6 ms), phần giao diện dưới 2 ms.

Thời gian nạp project giả lập (0,7–1,3 s với 3000 clip) là chi phí tạo 3000
lệnh sửa trong core, giống nhau ở hai bản, không phải chi phí giao diện.

## Vì sao chênh lệch

- QML tạo một đối tượng cho mỗi clip. Sau mỗi lệnh sửa, model đổi nên Repeater
  dựng lại toàn bộ delegate: chi phí tăng theo số clip của cả project, không phải
  số clip đang thấy trên màn hình.
- Widgets vẽ trực tiếp, chỉ duyệt các clip trong khoảng đang hiện, nên chi phí
  cuộn gần như không đổi khi project lớn lên.
- QML vẫn có thể nhanh nếu viết timeline thành một `QQuickItem` C++ tự vẽ bằng
  scene graph. Nhưng khi đó phần khó nhất lại là C++, và QML chỉ còn bọc các panel.

## Đề xuất: chọn Qt Widgets cho desktop

1. Đạt yêu cầu số 1 của dự án (không giật lag với project lớn) ngay bây giờ,
   có số đo, không cần thêm tối ưu.
2. Dock kéo thả, nhiều cửa sổ, phím tắt, menu: đúng kiểu phần mềm dựng phim
   trên máy tính, Widgets có sẵn và ổn định lâu năm.
3. Giao diện cảm ứng và hiệu ứng chuyển động là thế mạnh của QML, nhưng mobile
   đã dùng Flutter, nên desktop không cần.
4. Một ngôn ngữ (C++) cho cả desktop, dễ debug và review hơn khi trộn QML/JS.

Nếu chốt Widgets: giữ bản QML đến hết P0 để tham chiếu, rồi bỏ `desktop/quick/`
ở P1. Lớp `desktop/common/` đã tách sẵn nên việc bỏ không ảnh hưởng gì khác.

Trạng thái (13/11/2026): chưa chốt, giữ cả hai bản. Đã có số đo Windows (ở trên),
cùng xu hướng với Linux. macOS chưa đo được vì runner macOS của GitHub liên tục bị
hủy do hết chỗ; máy của nhóm (có GPU) chạy hai lệnh ở trên và gửi kết quả.

Cần đo lại trên máy thật (Windows, macOS có GPU) trước khi chốt hẳn: render phần
mềm trên Xvfb làm QML chậm hơn thực tế, nhưng không đổi được nguyên nhân chính là
dựng lại delegate theo số clip.
