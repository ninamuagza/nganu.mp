# nganu.game

Prototype client `raylib` untuk MMORPG 2D top-down yang sekarang bisa connect ke `nganu.mp`.

## Fitur awal

- Karakter utama bergerak dengan `WASD` atau arrow keys
- Kamera mengikuti player
- Map top-down besar dengan jalan, air, rumput, dan collision
- Layar login/connect untuk nama player, host, dan port
- Koneksi ENet ke `127.0.0.1:7777`
- Sync player join, leave, movement, dan chat sederhana
- NPC guide dan quest eksplorasi sederhana
- HUD sederhana: status koneksi, objective, chat box, dan panel pemain online

## Build

Client ini butuh `raylib` terpasang di sistem.

```bash
cd nganu.game
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## Run

```bash
./build/nganu-game
```

Jalankan server lebih dulu:

```bash
cd ../nganu.mp
./bin/game-server
```

## Controls

- `WASD` / arrow keys: bergerak
- `Left Shift`: lari
- `Enter`: connect / buka chat / kirim chat
- `E`: interaksi dengan NPC
- `Tab`: pindah field saat login
- `F1`: toggle debug overlay
- `Esc`: keluar

## Catatan

Detail jejak pengembangan ada di [`../progress.md`](../progress.md).
Checklist verifikasi lintas modul ada di [`../VERIFICATION_CHECKLIST.md`](../VERIFICATION_CHECKLIST.md).
Ringkasan kontrak protokol + asset flow ada di [`../PROTOCOL_ASSET_FLOW.md`](../PROTOCOL_ASSET_FLOW.md).
