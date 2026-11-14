# Đóng gói bản phát hành

Mọi bản cài được dựng trên GitHub Actions (`.github/workflows/release.yml`), không cần máy build riêng.

| Cách chạy | Kết quả |
|---|---|
| Đẩy tag `vX.Y.Z` (ví dụ `git tag v1.0.0 && git push origin v1.0.0`) | dựng tất cả và tạo **GitHub Release** kèm file cài + `SHA256SUMS.txt` |
| Actions → Release → *Run workflow* | dựng tất cả, file nằm ở mục *Artifacts* của lần chạy |

| Nền tảng | File | Ghi chú |
|---|---|---|
| Windows 10/11 x64 | `VideoStudio-X.Y.Z-windows-x64-setup.exe`, `...-portable.zip` | trình cài NSIS (shortcut Start menu + Desktop, gỡ cài trong Settings); FFmpeg bản LGPL từ vcpkg |
| macOS 11+ (Apple silicon) | `VideoStudio-X.Y.Z-macos-arm64.dmg` | media qua AVFoundation/VideoToolbox, không kèm FFmpeg |
| Linux (Ubuntu 24.04) | `videostudio_X.Y.Z_amd64.deb`, `.tar.gz` | dùng Qt và FFmpeg của hệ điều hành (`apt install ./videostudio_*.deb`) |
| Android 8+ | `VideoStudio-X.Y.Z-android.apk` (cài trực tiếp), `.aab` (Google Play) | |
| iOS 14+ | `VideoStudio-X.Y.Z-ios-unsigned.ipa` | chưa ký, xem bên dưới |

Mỗi bản desktop được khởi động thử trên CI (dựng timeline 50 clip, chụp màn hình) trước khi đóng gói.

## Ký số (tùy chọn, qua Secrets của repo)

Không có secret thì workflow vẫn chạy: Windows/macOS chưa ký (Windows SmartScreen cảnh báo; macOS mở bằng
chuột phải → Open), Android ký bằng khóa debug (cài được, nhưng Google Play không nhận).

| Secret | Dùng cho |
|---|---|
| `ANDROID_KEYSTORE_BASE64`, `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`, `ANDROID_KEY_PASSWORD` | ký APK/AAB để đưa lên Google Play (`base64 -w0 upload.jks`) |
| `WINDOWS_CERT_PFX_BASE64`, `WINDOWS_CERT_PASSWORD` | ký trình cài Windows (chứng thư Code Signing) |
| `MACOS_CERT_P12_BASE64`, `MACOS_CERT_PASSWORD`, `MACOS_SIGN_IDENTITY` | ký app macOS (Developer ID Application) |

**iOS**: cài lên iPhone hoặc đưa lên App Store cần tài khoản Apple Developer (99 USD/năm). Khi có tài khoản,
mở `mobile/ios/Runner.xcworkspace` trong Xcode, chọn Team, rồi *Product → Archive*; hoặc bổ sung chứng thư +
provisioning profile vào workflow và đổi `--no-codesign` thành `flutter build ipa`.

## Icon

`packaging/icons/make_icons.py` vẽ icon (không dùng ảnh bên ngoài) và ghi ra mọi kích thước: `.ico` (Windows),
`.icns` (macOS), PNG (Linux), mipmap Android, AppIcon iOS. Sửa script rồi chạy lại để đổi icon.

## Phiên bản

Lấy từ tag (`v1.2.3` → 1.2.3); CMake nhận `-DVSTUDIO_RELEASE_VERSION`, Flutter nhận `--build-name`, số build
Android/iOS là số thứ tự lần chạy workflow.
