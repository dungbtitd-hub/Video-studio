# Video Studio P0 benchmark

Máy đo: máy ảo Linux 4 luồng CPU, không GPU, FFmpeg 6.1 (libx264). Đây là số liệu tham khảo ban đầu; P0 yêu cầu đo lại trên máy Windows/macOS và điện thoại thật (đề án mục 10.1).

hardware threads: 4

### Timeline edits, 2000 video clips + 1000 audio clips (100 min timeline)

| operation                                                  |       avg |       p95 |       max | unit |
|------------------------------------------------------------|-----------|-----------|-----------|------|
| build whole timeline (append all clips)                    |  1187.865 |         - |         - | ms |
| split at playhead (all tracks)                             |     1.529 |     1.768 |     1.920 | ms |
| move last clip                                             |     1.158 |     1.370 |     1.807 | ms |
| trim end of last clip                                      |     0.966 |     1.161 |     1.511 | ms |
| undo                                                       |     0.196 |     0.273 |     0.348 | ms |
| redo                                                       |     0.991 |     1.109 |     1.242 | ms |
| publish snapshot for render threads (after an edit)        |     1.645 |     1.907 |     2.776 | ms |
| serialise project to JSON (save)                           |    24.274 |    28.648 |    28.648 | ms |

### Preview, 1920x1080 H.264 source, GOP 60 (preview size 960x540)

| operation                                                  |       avg |       p95 |       max | unit |
|------------------------------------------------------------|-----------|-----------|-----------|------|
| decode next frame at native size (sequential)              |     2.253 |     2.611 |    16.301 | ms |
| decode next frame straight to preview size (sequential)    |     2.576 |     3.683 |    12.801 | ms |
| random seek (scrub) to preview size                        |    34.294 |    45.039 |    45.039 | ms |
| build 540p proxy in background (whole clip, one-off)       |     662.5 |         - |         - | ms |
| random seek (scrub) on proxy                               |     9.615 |    13.467 |    13.467 | ms |
| render preview frame (decode + composite), cold            |     3.919 |     5.857 |    19.122 | ms |
| render same frames after split + move (from cache)         |     0.740 |     0.891 |     0.910 | ms |

### Preview, 3840x2160 H.264 source, GOP 60 (preview size 960x540)

| operation                                                  |       avg |       p95 |       max | unit |
|------------------------------------------------------------|-----------|-----------|-----------|------|
| decode next frame at native size (sequential)              |    41.239 |    48.372 |    90.073 | ms |
| decode next frame straight to preview size (sequential)    |     4.765 |     9.081 |    38.661 | ms |
| random seek (scrub) to preview size                        |   114.837 |   144.048 |   144.048 | ms |
| build 540p proxy in background (whole clip, one-off)       |     848.3 |         - |         - | ms |
| random seek (scrub) on proxy                               |     9.807 |    17.629 |    17.629 | ms |
| render preview frame (decode + composite), cold            |     5.816 |    11.134 |    41.502 | ms |
| render same frames after split + move (from cache)         |     0.652 |     0.789 |     0.850 | ms |
