# GLO — Game Latency Optimizer

<p align="center"><img src="docs/assets/glo-wordmark-dark-640.png#gh-light-mode-only" width="560" alt="GLO Game Latency Optimizer"><img src="docs/assets/glo-wordmark-light-640.png#gh-dark-mode-only" width="560" alt="GLO Game Latency Optimizer"></p>

GLO (Game Latency Optimizer) OSS chứa client Windows định tuyến game, relay Linux, secure transport, session admission, protocol, công cụ build và tài liệu dùng trong hệ sinh thái GLO. Dịch vụ GLO chính thức dùng các thành phần client/relay mã mở này; toàn bộ chức năng chỉ thuộc dịch vụ nằm ngoài cây OSS.

Client desktop có thể kết nối qua dịch vụ GLO chính thức hoặc provider bên thứ ba/self-host tương thích. Client chỉ probe relay được cung cấp và chỉ gửi lại phép đo nó tự quan sát.

## Dự án và nhận diện chính thức

- Website: <https://gloptimizer.com>
- Repository: <https://github.com/sonictype41/Game-Latency-Optimizer>

Mã nguồn dùng giấy phép MIT. Tên/logo GLO và tuyên bố liên kết chính thức được quản lý riêng trong [`BRAND_POLICY.md`](BRAND_POLICY.md). Mã nguồn mở không đồng nghĩa fork được quyền giả danh dự án hay dịch vụ GLO chính thức.

## Cấu trúc

- `app/` — client Windows và routing core.
- `relay/` — relay Linux và relay core dùng lại được.
- `session_key/` — grant bearer dùng một lần có chữ ký.
- `secure_transport/`, `protocol/` — secure control transport và wire format.
- `game_profiles/` — profile định tuyến game.
- `installer/` — source installer NSIS theo user và branding installer.
- `tools/` — công cụ build/package offline.
- `docs/` — kiến trúc, provider handoff và hướng dẫn relay.
- `vendor/`, `third_party/` — dependency local đã pin để build offline.

## Hai cách kết nối được hỗ trợ

GLO hỗ trợ **handoff qua provider** và **session config JSON thủ công**. Cả hai đều đi qua bước xác thực cấu hình và secure transport giống nhau. Mạng chính thức dùng handoff; JSON thủ công dành cho dịch vụ tự host hoặc bên thứ ba.

```text
A. Provider (Official / độc lập)                  B. Self-host / issuer độc lập
   Website cấp glo:// ngắn hạn                      Cấu hình JSON đã ký
          |                                                |
     Client nhận URI                            Client dán/nhập file JSON
          |                                                |
     HTTPS inspect: nhận candidate relay                    |
     Probe RTT từ máy người dùng                             |
     HTTPS redeem: nhận session config                       |
          +-----------------------+-------------------------+
                                  |
                      Kiểm tra session config nghiêm ngặt
                                  |
                       GLO client <=====> GLO relay
                                  |
                     Chỉ chuyển tiếp gameplay được hỗ trợ
```

**Handoff URI:** client inspect, tự đo candidate, redeem token rồi nhận grant/config; token có thời hạn ngắn và không chứa private key.

**JSON thủ công:** dán JSON, import file hoặc dùng `--config`. Không cần endpoint inspect/redeem, nhưng **vẫn phải có GSK2 grant 152 byte hợp lệ** được issuer ký và relay tin cậy. Ví dụ ở `tools/Session_Config.example.json` chỉ là mẫu, không thể kết nối bằng dữ liệu placeholder.

Xem [`docs/SELF_HOSTING.md`](docs/SELF_HOSTING.md), [`docs/SESSION_CONFIG.md`](docs/SESSION_CONFIG.md), [`docs/HANDOFF_PROVIDER.md`](docs/HANDOFF_PROVIDER.md) và [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## Build

Toolchain phải được cài sẵn. Dependency source được pin đã đi kèm repo; riêng runtime Wintun đã ký cho Windows sẽ được tải từ upstream chính thức khi chưa có và chỉ được chấp nhận nếu SHA-256 của archive và DLL khớp `third_party/wintun/INFO.json`. Đặt `GLO_OFFLINE=1` nếu muốn bắt buộc build offline với runtime đã có sẵn.

MSYS2 MINGW64:

```sh
pacman -S "$MINGW_PACKAGE_PREFIX-ccache"
python ./tools/fetch_wintun.py   # tùy chọn; build_app.sh tự fetch nếu thiếu
bash ./tools/build_relay.sh all
bash ./tools/build_app.sh
bash ./tools/build_installer.sh
bash ./tools/build_all.sh
```

Cache build nằm dưới `${GLO_BUILD_CACHE_DIR:-$TMPDIR/glo-build-cache}` và không được đóng vào release. Artifact cuối nằm trong `bin/` nếu không chỉ định output khác.

`VERSION` ghi compatibility của protocol/client; `RELEASE` nhận diện snapshot phát hành công khai tiếp theo. GitHub automation sẽ kiểm tra giá trị này, tạo tag tương ứng và publish release. Lịch sử phiên bản công khai nằm trong [`CHANGELOG.md`](CHANGELOG.md) và GitHub Releases, không hardcode trong README này.

## Self-host

**Bắt đầu:** [`docs/SELF_HOSTING.md`](docs/SELF_HOSTING.md) hướng dẫn từ Linux trống đến relay, ký grant và import JSON trên Windows. `tools/selfhost_issuer/` chỉ là issuer offline, không phải hệ thống tài khoản/API. Khi gặp ROUTE016, xem [`docs/DIAGNOSTICS.md`](docs/DIAGNOSTICS.md) và [`docs/TROUBLESHOOTING.md`](docs/TROUBLESHOOTING.md).

- [`docs/RELAY_OPERATOR.md`](docs/RELAY_OPERATOR.md) — vận hành relay.
- [`docs/HANDOFF_PROVIDER.md`](docs/HANDOFF_PROVIDER.md) — provider/handoff tương thích.
- [`SECURITY.md`](SECURITY.md) — ranh giới bảo mật và báo lỗi.
- [`BRAND_POLICY.md`](BRAND_POLICY.md) — quy tắc dùng tên/logo GLO.
- [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) — dependency bên thứ ba.

## Định tuyến (OSS 0.0.4-beta)

