# Project Progress

Tanggal acuan sesi ini: 2026-04-16

## Ringkasan

Repo ini sekarang sudah punya dua sisi yang saling terhubung:

- `nganu.mp`
  Server C++17 berbasis ENet + LuaJIT + plugin.
- `nganu.game`
  Client `raylib` untuk MMORPG 2D top-down.

## Jejak Progress

1. Analisis codebase awal

- Memetakan struktur repo.
- Mengidentifikasi `nganu.mp` sebagai source server utama.
- Menemukan area risiko di packet handling, plugin ABI, dan mode `--test`.

2. Membuat client game baru di `nganu.game`

- Menambahkan proyek CMake mandiri berbasis `raylib`.
- Membuat prototype top-down 2D yang playable.
- Menambahkan world besar, camera follow, collision, HUD, dan karakter lokal.

3. Menyambungkan client ke server

- Menambahkan ENet client di `nganu.game`.
- Menambahkan protocol client-side agar cocok dengan opcode server.
- Mengubah server agar mengirim snapshot, join, leave, dan pesan server.
- Mengganti dummy remote players dengan player sungguhan dari server.

4. Menambah fitur multiplayer dasar

- Chat sederhana antarpemain.
- Sinkronisasi nama player dari client ke server.
- Remote player smoothing agar pergerakan tidak terlalu patah.

5. Menambah flow login/connect

- Client sekarang tidak auto-connect diam-diam saat startup.
- Menambahkan layar login/connect untuk mengisi:
  - nama player
  - host server
  - port server
- Setelah handshake diterima, client masuk ke world.

6. Menambah NPC dan quest sederhana

- Menambahkan NPC guide bernama Luna dekat area spawn.
- Menambahkan interaksi `E` untuk menerima dan menyelesaikan quest.
- Menambahkan objective eksplorasi sederhana ke titik tertentu di map.
- Menambahkan state quest dasar di HUD.

7. Memindahkan quest dasar ke server Lua

- Menambahkan callback `OnPlayerMove(playerid)` dari server ke `main.lua`.
- Server Lua sekarang menyimpan progress quest per-player.
- Command `/quest` sekarang membaca status quest yang benar-benar berasal dari server.
- Progress quest dibersihkan saat player disconnect.

8. Merapikan state client

- Client sekarang memakai flow yang lebih formal:
  - `Boot`
  - `RetryWait`
  - `MainMenu`
  - `LoggingIn`
  - `World`
- Saat launch, client otomatis check update/kontinuitas koneksi ke server.
- Jika server tidak merespons, client menunggu countdown 10 detik lalu retry otomatis.
- Jika koneksi dan manifest awal sukses, client baru membuka main menu.
- Jika koneksi putus saat main game, client kembali ke main menu.

9. Menambahkan bootstrap manifest konten dari server

- Server sekarang menerima `UPDATE_PROBE` dari client.
- Server membalas `UPDATE_MANIFEST` berisi metadata konten awal.
- Manifest saat ini memuat:
  - `server_name`
  - `content_revision`
  - `world_name`
  - `map_id`
  - beberapa entri `asset=...`
- Client membaca manifest ini sebelum mengizinkan login/spawn.
- Ini menjadi fondasi agar map, gambar, dan behavior bisa bertahap dikirim oleh server, bukan dikemas statis di client.

10. Menambahkan transfer asset map sungguhan

- Client sekarang bisa mengirim `ASSET_REQUEST`.
- Server sekarang bisa membalas `ASSET_BLOB`.
- Implementasi awal dipakai untuk asset `map:starter_field`.
- Client menyimpan blob map ke cache lokal berdasarkan `content_revision`.
- Jika cache cocok tersedia, client memuat map dari cache tanpa meminta ulang ke server.
- `World` client sekarang bisa dibangun dari data map yang dikirim server, bukan hanya hardcoded di source.

11. Memindahkan source map ke file `.map`

- Map `starter_field` sekarang punya file sumber nyata di `nganu.mp/assets/maps/starter_field.map`.
- Server tidak lagi membentuk data map dari string hardcoded di C++.
- Server sekarang membaca file `.map` lalu mengirim hasilnya ke client sebagai blob asset.
- Format awal `.map` didokumentasikan di `nganu.mp/assets/maps/FORMAT.md`.
- Struktur ini disiapkan supaya nanti map editor bisa menulis file `.map` yang langsung dipakai pipeline yang sama.

12. Menaikkan format map untuk layer dan property

- Format `.map` sekarang tidak lagi cuma collision dasar.
- Sudah ada dukungan untuk:
  - `spawn`
  - `property`
  - `layer`
  - `object`
- `World` client sekarang menyimpan:
  - metadata map
  - spawn point
  - layer descriptors
  - object markers
  - properties map
- Render image final belum aktif, tapi kontrak data untuk asset gambar dan sifat map sudah disiapkan.

13. Menghubungkan gameplay lokal ke object map

- Posisi spawn player sekarang mengikuti `spawn` dari map.
- NPC guide dan trigger quest starter sekarang dibaca dari `object=...` di map.
- Quest panel mulai memakai title/description dari data map.
- Property map seperti `climate` dan `music` mulai dipakai di HUD/debug overlay.

14. Mendorong render dan interaksi jadi lebih map-driven

- Client sekarang punya lookup object map langsung dari `World`.
- Marker quest dan interaksi `E` ke NPC sekarang melakukan lookup ke object map, bukan hanya mengandalkan koordinat hardcoded.
- `layer=image` sekarang mulai memberi efek visual map-driven berbasis asset reference, walau masih fallback procedural dan belum texture final.
- Ini menyiapkan jalur supaya nanti asset bitmap sungguhan tinggal menggantikan renderer fallback, bukan mengganti format data lagi.

15. Mulai memakai atlas PNG nyata

- Asset atlas yang ditaruh di `nganu.mp/assets` sekarang mulai dipakai client.
- Layer map sekarang bisa menunjuk atlas source rect dengan bentuk `filename@x@y@w@h`.
- Renderer `World` sekarang bisa men-tile source rect atlas untuk layer ground, road, dan water.
- Jadi visual map tidak lagi murni warna/procedural; sudah mulai memakai asset gambar nyata dari project.

16. Menambahkan sprite object dari atlas

- Object map sekarang bisa punya property `sprite`.
- Nilai `sprite` memakai format atlas rect yang sama seperti layer image.
- NPC guide sekarang bisa dirender dari sprite atlas bila property itu tersedia.
- Prop map sederhana juga mulai bisa dirender dari sprite atlas.
- Ini menjadi jembatan ke map editor yang nanti cukup mengatur sprite object di file `.map`.

17. Menambahkan atribut sprite object dan avatar atlas

- Object map sekarang mulai mendukung:
  - `pivot`
  - `facing`
  - `z`
- Contoh data ini sudah dipakai di `starter_field.map`.
- Draw order prop sekarang mulai mempertimbangkan `z-layer` sederhana.
- Player local/remote juga mulai memakai sprite atlas statis sebagai pengganti bentuk lingkaran saat asset tersedia.

18. Mengurangi hardcode konten di client

- Dialog Luna sekarang dibaca dari property object map.
- Title/description/complete text quest starter sekarang dibaca dari object trigger map.
- Sprite player arah utara/timur/barat/selatan sekarang dibaca dari property map, bukan string hardcoded di `Game.cpp`.
- Fallback `World` sekarang dibuat netral dan tidak lagi membawa konten spesifik `starter_field` sebagai data bawaan.

19. Membagi peran `.map` dan `.lua` lebih sehat

- Server sekarang punya parser `.map` sendiri lewat `MapData`.
- Server memuat object/property map dan menggunakannya untuk event runtime.
- Ditambahkan event server-side:
  - `OnMapObjectInteract(playerid, object_index)`
  - `OnMapTriggerEnter(playerid, object_index)`
- Lua sekarang bisa membaca data map lewat builtin seperti:
  - `GetMapId()`
  - `GetMapProperty(key)`
  - `GetMapObjectId(index)`
  - `GetMapObjectKind(index)`
  - `GetMapObjectProperty(index, key)`
- Client sekarang mengirim interaksi object ke server.
- Objective panel client tidak lagi menjadi truth quest lokal; objective text sekarang dikirim dari server/Lua.

20. Menambahkan binding formal `script` dari map ke Lua

- `.map` sekarang mulai mendukung `property=map_script,...` dan `script:...` pada object.
- Lua tidak lagi harus mengandalkan `object_id == "luna"` untuk memutuskan behavior.
- `main.lua` sekarang memakai registry handler berbasis nama script, misalnya:
  - `npc_luna`
  - `trigger_starter_road`
- Ini menyiapkan jalur jangka panjang agar map editor cukup mengatur binding script di data map, sementara Lua memegang implementasi behavior-nya.

21. Memecah Lua menjadi modul map/NPC/trigger

- `main.lua` sekarang hanya bootstrap tipis.
- Logic map dipindah ke `scripts/maps/greenfields_main.lua`.
- Logic NPC Luna dipindah ke `scripts/npc/luna.lua`.
- Logic trigger road marker dipindah ke `scripts/triggers/starter_road.lua`.
- State quest starter dipindah ke `scripts/quests/starter_quest.lua`.
- Struktur ini lebih cocok untuk jangka panjang karena content behavior tidak lagi menumpuk dalam satu file Lua besar.

22. Menambahkan multi-map dan perpindahan map

- Server sekarang memuat semua file `.map` di `assets/maps`, bukan hanya satu map tunggal.
- State `current map` sekarang disimpan per-player di server.
- Snapshot, join/leave, movement, name update, dan chat sekarang difilter per-map.
- Ditambahkan `portal` object di data map untuk perpindahan antar map.
- Ditambahkan map kedua `crossroads.map` dan portal dua arah dengan `starter_field.map`.
- Client sekarang bisa menerima event `MAP_TRANSFER`, meminta asset map tujuan, lalu reload world tanpa relaunch.
- Lua builtin sekarang punya akses map aktif per-player seperti:
  - `GetPlayerMapId(playerid)`
  - `GetPlayerMapProperty(playerid, key)`
  - `GetPlayerMapObjectProperty(playerid, object_index, key)`
- `main.lua` sekarang dispatch ke map module berdasarkan `map_script` dari map aktif player, bukan asumsi satu map global.

## Status Saat Ini

### Server

- Build berhasil.
- Bisa listen di port `7777`.
- Menyimpan posisi player.
- Menyimpan nama player.
- Broadcast:
  - movement
  - chat
  - player join/leave
  - player name updates

### Client

- Build berhasil.
- Punya boot update check sebelum menu.
- Punya retry countdown 10 detik bila server tidak merespons.
- Punya main menu yang menunggu manifest konten dari server.
- Punya world top-down playable.
- Bisa connect ke server ENet.
- Bisa kirim nama player.
- Bisa kirim chat.
- Bisa menerima snapshot dan update player lain.
- Bisa menerima manifest konten awal dari server.
- Bisa meminta dan menerima blob map dari server.
- Bisa cache map asset lokal berdasarkan revision.
- Source map server sekarang sudah berupa file `.map`.
- Format map sekarang sudah punya fondasi untuk layer visual, object, dan property.
- Gameplay lokal awal sekarang mulai memakai object/property dari map.
- Punya NPC interactable dan quest lokal sederhana.
- Punya progress quest dasar di server-side Lua.

## Cara Menjalankan

### Server

```bash
cd /path/to/nganu.mp/nganu.mp
./bin/game-server
```

### Client

```bash
cd /path/to/nganu.mp/nganu.game
./build/nganu-game
```

## Kontrol Client

- `WASD` / arrow keys: bergerak
- `Shift`: lari
- `Enter`: buka chat / kirim chat
- `E`: interaksi dengan NPC
- `Tab`: pindah field saat di main menu
- `Esc`: batal input chat
- `F5`: check update / reconnect ke server
- `F1`: toggle debug

## Batasan Saat Ini

- Belum ada login/auth yang sesungguhnya.
- Belum ada download file asset binary sungguhan; baru manifest/bootstrap metadata.
- Transfer asset real baru tahap map text/blob, belum image binary atau behavior blob.
- Layer gambar map baru tahap descriptor/reference asset, belum render tileset/sprite final.
- Belum ada persistence akun/karakter.
- Belum ada combat, inventory, NPC, atau quest system yang nyata.
- Server belum authoritative penuh untuk validasi movement/collision.
- Client masih memakai primitive shapes, belum aset final/sprite animation.

## Kandidat Langkah Berikutnya

1. Login/auth yang nyata.
2. Interaksi NPC dan quest sederhana.
3. Animation states untuk idle/walk.
4. Server-authoritative movement validation.
5. Chat UI yang lebih lengkap.
6. Spawn system dan region/map data yang lebih formal.
7. Streaming dan caching asset dari server:
   - map data
   - image/sprite bytes
   - behavior descriptors

## Update Terbaru

- Pipeline asset sekarang dipisah antara `map`, `map_image`, dan `character_image`.
- Folder fisik server sekarang juga mulai dipisah:
  - `nganu.mp/assets/map_images`
  - `nganu.mp/assets/characters`
- Format ref atlas di `.map` sekarang mendukung domain eksplisit seperti:
  - `map:terrain_atlas.png@0@0@32@32`
  - `character:base_out_atlas.png@608@0@32@32`
- Proyek editor atlas baru ada di [`nganu.atlas`](./nganu.atlas) untuk memilih source rect atlas dan menyalin ref asset yang sesuai dengan pipeline runtime.
- `nganu.atlas` sekarang juga punya mode map editor dasar untuk paint `stamp`, toggle `blocked/water`, set `spawn`, dan save `.map`.
- Runtime sekarang diarahkan ke satu world besar default [`overworld.map`](./nganu.mp/assets/maps/overworld.map), dengan `portal` default sebagai teleport dalam map yang sama.
- Konsep `region` sekarang dipakai sebagai object map untuk area besar seperti `Greenfields` dan `Crossroads`, lalu client HUD membaca region aktif dari posisi player.
- `nganu.atlas` object mode sekarang juga mendukung placement `region`, jadi editor ikut selaras dengan desain overworld tunggal.
