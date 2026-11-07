# Pixel Cup Soccer — port muOS (NextOS so-loader)

Port Android `com.BatoviGamesStudio.PixelCupSoccer19` (Unity 2022.3.62f3,
IL2CPP, arm64-v8a) cho máy cầm tay Linux AArch64 (muOS, ArkOS, ROCKNIX...).

Loader `pixelcupsoccer-nextos` chính là loader Unity 2022.3/IL2CPP của port
**Skateboard Party 3** trong NextOS Universal Ports (GPL-3.0). Loader nạp
`libmain.so`, `libunity.so`, `libil2cpp.so` gốc của APK, tự giả lập JNI,
bionic và vòng đời UnityPlayer; hình qua EGL/GLES2, tiếng qua SDL2.

## Cài đặt trên muOS

Cách 1 — PortMaster autoinstall:

1. Chép `pixelcupsoccer.zip` vào `/mnt/mmc/MUOS/PortMaster/autoinstall/`
   (hoặc `/mnt/sdcard/...` nếu dùng thẻ thứ 2).
2. Mở PortMaster một lần để nó tự cài.
3. Chép file APK vào thư mục `ports/pixelcupsoccer/gamedata/` mà PortMaster vừa
   tạo (thường là `/mnt/mmc/ports/pixelcupsoccer/gamedata/`).

Cách 2 — chép tay:

```
ROMS/Ports/Pixel Cup Soccer.sh
ROMS/Ports/pixelcupsoccer/          (cả thư mục)
ROMS/Ports/pixelcupsoccer/gamedata/Pixel-Cup-Soccer-Mobile-v1-b321-full.apk
```

Launcher tìm `/<directory>/ports/pixelcupsoccer` trước, không có thì dùng thư
mục `pixelcupsoccer/` nằm cạnh file `.sh`.

## Lần chạy đầu

NXExtract kiểm tra APK, giải nén `lib/` và `assets/bin/Data` (~600 MB) rồi mới
vào game. Hook `nxextract/merge-datapack.py` gộp `datapack.unity3d`
(UnityDataAssetPack: `resources.assets` + `.resS`) vào `data.unity3d`, vì trên
Android gói này được mount qua Java Play Asset Delivery mà loader không có. Cần khoảng **1,8 GB trống** (dữ liệu + 1 GB dự phòng mà recipe yêu
cầu). Sau khi cài xong có thể xoá APK trong `gamedata/`.

## Nhật ký lỗi

- `pixelcupsoccer/log.txt` — lần chạy hiện tại (dòng `RUNTIME DIAGNOSIS` ở cuối)
- `pixelcupsoccer/log.prev.txt` — lần trước
- `pixelcupsoccer/nxextract.log` — bước giải nén APK

## Điều khiển

Gamepad được Unity đọc như tay cầm Xbox (vị trí nút). Analog phải điều khiển con
trỏ cho các màn hình cảm ứng, R3/R1 = chạm. **Select + Start** để thoát.

## Giới hạn đã biết

- Chưa được thử trên máy thật — loader được viết cho Skateboard Party 3, nên
  các hook riêng của game đó (`[sk3/age]`, `[sk3/lang]`) sẽ chỉ ghi log
  "classe ausente" rồi bỏ qua.
- Không có Google Play Games, quảng cáo, mua hàng; chơi offline.
- Package name loader báo cho Unity là của Skate3, nên save nằm trong
  `pixelcupsoccer/home/` theo tên đó — không ảnh hưởng gameplay.
"# pixelcupsoccer-port" 
