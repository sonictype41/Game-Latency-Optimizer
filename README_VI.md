# GLO — Game Latency Optimizer

<p align="center"><img src="docs/assets/glo-wordmark-dark-640.png#gh-light-mode-only" width="560" alt="GLO Game Latency Optimizer"><img src="docs/assets/glo-wordmark-light-640.png#gh-dark-mode-only" width="560" alt="GLO Game Latency Optimizer"></p>

GLO (Game Latency Optimizer) OSS chứa client Windows định tuyến game, relay Linux, secure transport, session admission, protocol, công cụ build và tài liệu dùng trong hệ sinh thái GLO. Dịch vụ GLO chính thức dùng các thành phần client/relay mã mở này; toàn bộ chức năng chỉ thuộc dịch vụ nằm ngoài cây OSS.

Client desktop có thể kết nối qua dịch vụ GLO chính thức hoặc provider bên thứ ba/self-host tương thích. Client chỉ probe relay được cung cấp và chỉ gửi lại phép đo nó tự quan sát.

## Dự án và nhận diện chính thức

- Website: <https://gloptimizer.com>
- Endpoint dịch vụ chính thức: `https://api.gloptimizer.com`
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

## Handoff provider

```text
service / provider
    |
    | glo:// handoff ngắn hạn
    v
GLO desktop client
    |
    | /app/handoff/inspect
    | <- danh sách relay + relay identity key
    |
    | client tự probe candidate
    |
    | /app/handoff/redeem + measurements
    v
service/provider trả session config
    |
    v
GLO client <============================> GLO relay
```

Session config cuối là dữ liệu nghiêm ngặt, chỉ chứa material cần cho relay/session và không chứa credential dịch vụ, private key hay đường dẫn executable local. Xem [`docs/HANDOFF_PROVIDER.md`](docs/HANDOFF_PROVIDER.md) và [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

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

- [`docs/RELAY_OPERATOR.md`](docs/RELAY_OPERATOR.md) — vận hành relay.
- [`docs/HANDOFF_PROVIDER.md`](docs/HANDOFF_PROVIDER.md) — provider/handoff tương thích.
- [`SECURITY.md`](SECURITY.md) — ranh giới bảo mật và báo lỗi.
- [`BRAND_POLICY.md`](BRAND_POLICY.md) — quy tắc dùng tên/logo GLO.
- [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) — dependency bên thứ ba.
