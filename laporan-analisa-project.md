# Laporan Analisa Project

Tanggal analisa: 16 April 2026

## Ringkasan Eksekutif

Repository ini terdiri dari tiga aplikasi C++17 berbasis CMake:

- `nganu.mp`: server multiplayer berbasis ENet, LuaJIT, dan plugin C ABI.
- `nganu.game`: client prototype berbasis `raylib`.
- `nganu.atlas`: editor atlas dan map berbasis `raylib`.

Secara teknis, fondasi server sudah cukup solid untuk tahap prototipe vertical slice. Server berhasil menjalankan smoke test, memuat 3 map, plugin, dan runtime Lua tanpa error. Di sisi lain, client dan editor masih membawa jejak refactor struktur direktori lama (`game-server`) sehingga sebagian dokumentasi tidak sinkron, build client baru gagal, dan editor berisiko tidak menemukan aset saat dijalankan dari tree repo saat ini.

## Hasil Verifikasi

Pemeriksaan yang dijalankan:

- `./bin/game-server --test` dari `nganu.mp`: berhasil.
- `cmake -S . -B /tmp/nganu-atlas-cmake-check` dari `nganu.atlas`: berhasil.
- `cmake -S . -B /tmp/nganu-game-cmake-check` dari `nganu.game`: gagal pada konfigurasi karena source ENet mengarah ke path yang sudah tidak ada.

## Arsitektur dan Alur Sistem

### 1. Server `nganu.mp`

Komponen utama server:

- `Runtime`: parser konfigurasi `key=value`.
- `Network`: wrapper ENet untuk host, peer, send/broadcast, disconnect.
- `Server`: orkestrator startup, tick loop, koneksi pemain, state map, chat, movement, transfer map, asset manifest.
- `LuaRuntime`: memuat gamemode LuaJIT dan memanggil hook event.
- `Builtins`: bridge Lua ke server/network.
- `PluginManager`: loader plugin `.so/.dll` dengan ABI stabil.
- `MapData`: parser `.map`, object, stamp, collision, water, dan metadata atlas.

Alur startup server:

1. Baca `server.cfg`.
2. Muat semua file `.map` dari folder map.
3. Hitung `content_revision` dari isi map dan aset terkait.
4. Inisialisasi ENet.
5. Muat plugin.
6. Register builtin Lua.
7. Muat gamemode Lua dan panggil `OnGameModeInit`.

Alur runtime penting:

- Client connect lalu menerima `HANDSHAKE` berisi `playerId`.
- Client meminta `UPDATE_MANIFEST`.
- Server mengirim daftar aset berbasis `content_revision`.
- Client meminta map/image/meta yang dibutuhkan.
- Setelah map termuat, movement, chat, interaksi object, trigger, dan transfer map berjalan lewat opcode ENet.

### 2. Client `nganu.game`

Client sudah lebih dari sekadar prototype render:

- Memiliki boot/update-check screen sebelum masuk menu.
- Melakukan content bootstrap dari server manifest.
- Menyimpan aset hasil download ke cache lokal.
- Memuat map dari asset blob server.
- Menangani snapshot pemain, join/leave, movement, nama, objective text, dan map transfer.
- Menjalankan collision lokal melalui parser map yang paralel dengan server.

Arsitekturnya cukup rapi:

- `NetworkClient` fokus pada ENet dan decoding paket.
- `World` fokus pada parsing map dan rendering/collision.
- `Game` memegang state UI, gameplay, sinkronisasi network, cache, dan HUD.

### 3. Editor `nganu.atlas`

Editor sudah mencakup workflow penting:

- Scan atlas map/character.
- Seleksi source rect atlas.
- Paint `stamp=` ke layer map.
- Toggle `blocked` dan `water`.
- Set spawn.
- Load/save file `.map`.

Secara fungsi, editor sudah sejalan dengan format map server dan client. Masalah utama editor saat ini bukan pada model datanya, tetapi pada hardcoded path asset yang masih mengacu ke struktur lama.

## Temuan Utama

### Temuan 1: Build fresh untuk client rusak

Lokasi:

- [nganu.game/CMakeLists.txt](./nganu.game/CMakeLists.txt)

Masalah:

- Source ENet dan include directory pada histori refactor sempat mengarah ke path server lama yang sudah tidak dipakai.
- Struktur repo aktual hanya memiliki `nganu.mp/vendor/enet/...`.

Dampak:

- `cmake -S . -B /tmp/nganu-game-cmake-check` gagal pada tahap generate.
- Developer baru tidak bisa build client dari source tree saat ini tanpa mengubah path manual.

### Temuan 2: Editor atlas memakai root asset yang salah

Lokasi:

- [nganu.atlas/src/AtlasEditor.cpp](./nganu.atlas/src/AtlasEditor.cpp)

Masalah:

- Root asset dan map pada histori refactor sempat di-hardcode ke struktur server lama yang sudah tidak dipakai.
- Folder itu tidak ada pada struktur repo saat ini.

Dampak:

- Editor berpotensi membuka workspace kosong walau aset sebenarnya ada.
- Risiko false impression seolah atlas/map tidak tersedia.

### Temuan 3: Dokumentasi masih tertinggal dari refactor struktur repo

Lokasi:

- [nganu.game/README.md](./nganu.game/README.md)
- [nganu.atlas/README.md](./nganu.atlas/README.md)

Masalah:

- README sempat menyebut struktur folder server lama.
- Instruksi run/build tidak lagi sesuai dengan layout repo aktual.

Dampak:

- Onboarding developer melambat.
- Sulit membedakan error dokumentasi vs error implementasi.

### Temuan 4: Server sudah punya fitur lebih maju daripada dokumentasi

Lokasi:

- [nganu.mp/src/core/Server.cpp](./nganu.mp/src/core/Server.cpp)
- [nganu.game/src/Game.cpp](./nganu.game/src/Game.cpp)

Fakta:

- Sudah ada multi-map.
- Sudah ada content manifest dan asset blob delivery.
- Sudah ada portal/trigger berbasis object map.
- Sudah ada map transfer per pemain.

Dampak:

- Dokumentasi saat ini mengecilkan kapabilitas sistem.
- Knowledge transfer ke kontributor baru jadi tidak optimal.

## Kekuatan Project

- Arsitektur modul cukup jelas dan pemisahan tanggung jawab rapi.
- Format `.map` konsisten dipakai server, client, dan editor.
- Pendekatan data-driven sudah terlihat kuat.
- Server memiliki validasi movement dasar dan kontrol ukuran paket.
- Sistem scripting Lua dan plugin C ABI memberi fleksibilitas yang baik.
- Client sudah memiliki bootstrapping aset dari server, bukan sekadar hardcoded local asset.

## Risiko Teknis

- Parser map dan protokol dipelihara paralel di server dan client. Tanpa test kompatibilitas, perubahan format mudah membuat drift.
- Asset pipeline masih bergantung pada path relatif dan current working directory.
- Build artifact lama di dalam repo menunjukkan ada refactor path yang belum dibersihkan sepenuhnya.
- Belum terlihat automated test untuk client/editor atau kontrak protokol lintas modul.

## Prioritas Rekomendasi

Prioritas tinggi:

1. Perbaiki seluruh referensi path `game-server` yang sudah obsolete pada client, editor, dan README (status: sudah diselaraskan ke `nganu.mp/...`).
2. Tambahkan satu jalur verifikasi lintas modul minimal: configure + build semua target + smoke test server.
3. Dokumentasikan arsitektur protokol dan asset bootstrap karena itu sekarang bagian inti sistem.

Prioritas menengah:

1. Satukan definisi protokol dan format map yang duplikatif antara server dan client.
2. Kurangi ketergantungan pada current working directory untuk asset discovery.
3. Tambahkan test parser `.map` dan validasi manifest/asset blob.

Prioritas rendah:

1. Rapikan build output yang ikut tersimpan di repo agar analisa source lebih bersih.
2. Tambahkan diagram sederhana untuk alur connect, manifest, asset request, dan map transfer.

## Kesimpulan

Project ini sudah melewati fase prototype mentah dan sebenarnya punya fondasi gameplay/networking yang cukup menjanjikan, terutama di sisi server dan pipeline map data. Masalah paling mendesak bukan kekurangan fitur, tetapi inkonsistensi hasil refactor struktur direktori yang membuat client fresh build rusak dan editor/doc berpotensi menyesatkan. Jika mismatch path ini dibereskan lebih dulu, repo akan jauh lebih siap untuk iterasi fitur berikutnya.
