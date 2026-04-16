# Verification Checklist

Gunakan checklist ini sebelum merge perubahan lintas modul agar perubahan server/client/editor tetap sinkron.

## 1) Configure + Build (fresh build directory)

> Disarankan pakai build directory di `/tmp` untuk menghindari cache lama.

```bash
cmake -S /path/to/repo/nganu.mp -B /tmp/nganu-mp-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/nganu-mp-build --parallel

cmake -S /path/to/repo/nganu.game -B /tmp/nganu-game-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/nganu-game-build --parallel

cmake -S /path/to/repo/nganu.atlas -B /tmp/nganu-atlas-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/nganu-atlas-build --parallel
```

## 2) Server smoke test

```bash
cd /path/to/repo/nganu.mp
./bin/game-server --test
```

`--test` sekarang juga mengecek validasi parser map minimum:
- map termuat
- ukuran map/tile valid
- `content_revision` tidak kosong

## 3) Manual functional sanity

- Jalankan server: `./bin/game-server`
- Jalankan client: `./build/nganu-game`
- Jalankan atlas editor: `./build/nganu-atlas`
- Cek alur dasar:
  - client menerima manifest
  - client request map asset
  - world load sukses
  - map transfer/portal tetap bekerja
