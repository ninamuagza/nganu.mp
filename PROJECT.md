# PROJECT

## Visi

Arsitektur project `nganu` diarahkan menjadi game `2D top-down MMORPG` seperti `Curse of Aros`, dengan karakter utama berikut:

- Client harus sangat kecil, targetnya tetap ringan dan idealnya `< 10 MB` untuk bootstrap awal.
- Saat game dibuka, client melakukan koneksi ke server, memeriksa revision content, lalu mengunduh patch/update content yang dibutuhkan.
- Content hasil patch dari server kemudian dipakai langsung oleh game tanpa mewajibkan rebuild atau redistribusi client.
- Server bersifat authoritative untuk state dunia, player, entity, interaksi, dan validasi gameplay.
- C++ server bukan tempat hardcode logic game harian. C++ berfungsi sebagai `core runtime`, sedangkan logic runtime game ditulis di `Lua`.

Project ini bukan diarahkan menjadi client tebal yang membawa semua asset, map, dan logic dari awal. Arah utamanya adalah `thin client + authoritative server + live content delivery`.

## Sasaran Produk

Target akhir project:

- Player mengunduh executable client yang kecil.
- Saat client dijalankan, client meminta manifest/update dari server.
- Server memberi tahu content revision aktif, daftar asset/map/data yang dibutuhkan, lalu client mengunduhnya ke cache lokal.
- Setelah content siap, player masuk ke world yang sepenuhnya dikontrol server.
- Perubahan content seperti map, atlas, sprite, metadata, scripted event, quest, NPC behaviour, drop, objective, dan balancing dapat diubah dari sisi server tanpa harus update binary client.

Secara praktis, `nganu` harus terasa seperti game online yang hidup:

- world bisa berubah lewat patch content
- behaviour game bisa berubah lewat script
- binary client relatif jarang berubah
- binary server C++ stabil dan menjadi fondasi runtime jangka panjang

## Prinsip Arsitektur

### 1. Thin Client

Client bertanggung jawab untuk:

- bootstrap koneksi
- menerima manifest/update info
- download dan cache asset/content
- render world dan UI
- mengirim state movement yang relevan ke server
- melakukan prediction ringan bila diperlukan
- menerima state authoritative dan melakukan reconciliation

Client tidak boleh menjadi sumber kebenaran gameplay.

Yang tidak boleh dijadikan tanggung jawab utama client:

- validasi final movement
- hasil combat
- hasil interaksi object
- quest progression final
- ekonomi/game state persisten
- authority terhadap entity state

### 2. Authoritative Server

Server bertanggung jawab untuk:

- koneksi session player
- authority posisi/state player
- authority map transfer
- authority NPC/entity/object interaction
- authority quest state, reward, combat result, dan progression
- authority revision content yang aktif
- distribusi manifest dan asset blob ke client

Server harus dianggap sebagai satu-satunya sumber kebenaran untuk game state yang penting.

### 3. C++ Server Sebagai Core Runtime

Server C++ seharusnya berfungsi sebagai runtime core, bukan tempat menulis logic game spesifik.

Tanggung jawab C++:

- network transport
- serialization / protocol
- player session management
- map loading/runtime data access
- asset manifest delivery
- authoritative state plumbing
- scheduler / tick loop
- plugin bridge
- Lua runtime host
- persistence integration
- validation primitives

Yang seharusnya tidak di-hardcode di C++ kecuali benar-benar generik:

- quest logic
- NPC dialogue flow
- progression rules
- reward rules
- scripted trigger behaviour
- event dunia
- spawn rules yang berubah-ubah
- balancing gameplay
- command/game mode logic yang sebetulnya cocok ditulis di script

### 4. Lua Sebagai Runtime Logic Layer

Semua logic yang sering berubah harus diarahkan ke `Lua`.

Contoh logic yang idealnya hidup di `scripts/*.lua`:

- quest flow
- trigger enter/leave
- NPC interaction
- portal rule
- map-specific scripted event
- tutorial flow
- command admin/gameplay
- mission objective
- drop/reward rule
- seasonal event

Dengan begitu:

- C++ tetap stabil
- iteration gameplay lebih cepat
- perubahan live ops lebih murah
- game mode tidak mengotori core engine

## Desain Hubungan Client dan Server

### Tahap Boot

Urutan boot yang ingin dicapai:

1. Client start.
2. Client connect ke server.
3. Server kirim handshake dan update manifest.
4. Client membandingkan revision lokal dengan revision server.
5. Client meminta asset/map/meta yang belum ada atau outdated.
6. Server mengirim blob content.
7. Client simpan ke cache lokal.
8. Setelah content minimum siap, client boleh masuk ke menu/login/world.

Client harus dianggap sebagai `runtime viewer + input endpoint`, bukan paket content utama.

### Tahap Runtime

Saat gameplay berjalan:

- client mengirim posisi/state movement ke server
- server memproses logic authoritative
- server mengirim state/result
- client menampilkan hasilnya

Untuk area realtime seperti movement, model yang diinginkan:

- client mengirim posisi movement hasil simulasi lokal
- server memvalidasi movement, walkability, dan batas anomali
- server boleh mengoreksi bila posisi tidak valid
- authority final untuk accept/reject state tetap ada di server

Catatan keputusan saat ini:

- project untuk sekarang tidak memaksakan model `client sends input intent`
- movement `client sends position` dianggap lebih pragmatis untuk prototipe ini, terutama dengan pertimbangan latensi, kesederhanaan infrastruktur, dan biaya implementasi
- konsekuensinya, server validation harus tetap kuat agar model ini tidak berubah menjadi client-authoritative penuh

### Tahap Content Delivery

Content yang idealnya bisa dikirim dari server:

- map data
- map metadata
- atlas metadata
- sprite/texture/image
- UI theme image/atlas
- UI layout document
- entity visual data
- object presentation data
- scripted content dependencies

Ke depan, sistem ini juga sebaiknya mendukung:

- content revision per world
- partial patch
- cache invalidation yang jelas
- integrity checking
- fallback ke cache lokal bila server content belum berubah

### Tahap UI Runtime

UI client tidak boleh bergantung penuh pada warna, frame, dan layout hardcoded di binary.

Target arah UI runtime:

- layout window dikirim sebagai dokumen data-driven, misalnya `data:ui/*.json`
- widget C++ hanya menangani behaviour runtime, input, state binding, dan rendering hook
- skin/theme UI dikirim sebagai asset server, misalnya `ui_image:*` dan metadata atlas yang relevan
- pergantian theme, panel frame, button skin, modal frame, dan visual window tidak boleh mewajibkan rebuild client
- sistem UI harus mendukung layering eksplisit seperti `Window`, `Popup`, `Modal`, dan `Tooltip`

Konsekuensinya:

- `layout/config` dan `theme/skin` adalah dua kontrak terpisah
- window seperti inventory, objective journal, modal, character sheet, dan quest log harus bisa berbagi framework yang sama
- fallback visual lokal masih boleh ada, tetapi bukan satu-satunya jalur render

## Batasan Binary Client

Binary client harus dijaga tetap kecil dan fokus.

Yang idealnya ada di bundle client:

- executable
- runtime library minimum
- font/UI minimum
- asset placeholder minimum bila benar-benar perlu
- fallback skin minimum bila asset UI dari server belum siap

Yang idealnya tidak dibundel besar-besaran:

- semua map
- semua sprite dunia
- semua atlas final
- content live ops
- data world yang sering berubah

Tujuan akhirnya adalah membuat onboarding ringan:

- download client kecil
- buka game
- patch content dari server
- langsung main

## Batasan C++ Core

Agar arah project tidak melenceng, beberapa aturan desain perlu dijaga:

- Jangan hardcode content game di C++ bila masih bisa dipindah ke data atau Lua.
- Jangan hardcode quest, NPC flow, reward, atau event map di C++.
- Jangan menjadikan client sebagai authority gameplay.
- Jangan mengikat asset pipeline ke path statis yang rapuh.
- Jangan membuat protocol client-server drift; kontrak shared harus tunggal.
- Jangan menaruh rule gameplay yang sering berubah di layer engine.

## Non-Goals

Hal-hal berikut bukan tujuan utama project ini:

- Bukan single-player offline game dengan semua content dibundel permanen di client.
- Bukan arsitektur `fat client` yang menjadikan client sebagai sumber kebenaran gameplay.
- Bukan server C++ yang penuh hardcode quest, NPC flow, map event, dan balancing harian.
- Bukan patching model yang mewajibkan update binary client untuk setiap perubahan content kecil.
- Bukan editor-centric runtime, yaitu editor menentukan authority state saat game berjalan.
- Bukan target utama untuk membuat semua sistem gameplay langsung selesai di C++ sebelum Lua/data layer matang.
- Bukan prioritas awal untuk visual fidelity berat yang membuat bootstrap client membengkak.
- Bukan arsitektur yang mengandalkan path statis rapuh atau coupling antar modul yang membuat refactor kecil merusak pipeline content.
- Bukan target jangka dekat untuk menjadikan client mampu berjalan penuh tanpa server authoritative.
- Bukan model live service yang menggantungkan perubahan gameplay pada recompile server C++ untuk perubahan yang semestinya bisa ditulis di script.

## Architecture Decision

Keputusan arsitektur inti project ini:

### AD-001: Client harus tetap tipis

Keputusan:

- Client diperlakukan sebagai bootstrap runtime, renderer, cache content, dan endpoint input.

Alasan:

- menurunkan ukuran distribusi awal
- mempermudah onboarding
- memungkinkan patch content live dari server
- mengurangi kebutuhan rebuild client

Konsekuensi:

- client harus punya manifest/content cache yang stabil
- client tidak boleh menjadi authority gameplay
- asset pipeline harus mendukung fetch dari server

### AD-002: Server authoritative adalah sumber kebenaran

Keputusan:

- Semua state gameplay penting diputuskan server.

Alasan:

- mencegah cheat dan drift state
- menyederhanakan konsistensi multiplayer
- cocok untuk MMORPG persisten

Konsekuensi:

- client boleh mengirim posisi/state movement, selama tetap divalidasi server
- server harus punya jalur reconciliation
- gameplay logic penting tidak boleh bergantung pada asumsi client benar

### AD-002A: Movement memakai model client sends position

Keputusan:

- Untuk saat ini, movement realtime memakai model `client sends position`, bukan `client sends input intent`.

Alasan:

- lebih sederhana untuk prototipe yang sedang dibangun
- lebih pragmatis terhadap latensi dunia nyata
- lebih ringan untuk diintegrasikan dengan infrastruktur saat ini
- mengurangi kompleksitas prediction/replay pipeline di tahap awal

Konsekuensi:

- server wajib memvalidasi kecepatan, walkability, map boundary, dan state anomali
- server tetap harus punya mekanisme koreksi bila posisi client tidak valid
- model ini tidak boleh berkembang menjadi trust penuh ke client
- bila skala gameplay nanti menuntutnya, keputusan ini bisa dievaluasi ulang, tapi bukan prioritas saat ini

### AD-003: C++ server adalah runtime core, bukan gameplay layer utama

Keputusan:

- C++ server difokuskan untuk network, protocol, session, asset delivery, tick loop, runtime bridge, dan fondasi authoritative state.

Alasan:

- menjaga core tetap stabil
- menurunkan biaya perubahan gameplay
- mencegah core engine tercampur dengan rule game spesifik

Konsekuensi:

- rule gameplay generik boleh ada di C++, tapi rule game yang sering berubah harus dihindari
- contributor harus menahan diri untuk tidak menaruh quest/event/balancing langsung di C++

### AD-004: Lua adalah layer logic runtime

Keputusan:

- Logic gameplay yang sering berubah diarahkan ke `Lua`.
- API runtime yang dibutuhkan gameplay harus diekspos dari C++ ke Lua sebagai builtins/script functions yang rapi.

Alasan:

- iteration cepat
- live ops lebih mudah
- content dan gameplay bisa berkembang tanpa mengubah core server terus-menerus

Konsekuensi:

- C++ harus menyediakan API/script binding yang cukup
- event runtime harus diekspos rapi ke Lua
- desain feature baru harus selalu dievaluasi: apakah ini seharusnya data/Lua atau core C++

### AD-004A: Fungsi yang bisa diakses Lua adalah kontrak resmi runtime

Keputusan:

- Fungsi/builtins yang diekspos ke Lua diperlakukan sebagai kontrak runtime resmi antara core C++ dan gameplay script.

Alasan:

- gameplay script butuh akses aman ke capability runtime
- feature gameplay baru sering lebih tepat ditambah lewat builtins daripada hardcode flow baru di C++
- ini menjaga batas yang jelas antara `engine capability` dan `game logic`

Konsekuensi:

- penambahan fungsi Lua-accessible adalah jalur utama untuk memperluas gameplay runtime
- builtins harus tetap generik, reusable, dan tidak mengunci satu quest/map tertentu
- nama, behaviour, dan parameter fungsi script harus dijaga stabil dan terdokumentasi
- bila sebuah kebutuhan gameplay bisa dipenuhi dengan menambah builtin generik, itu lebih diutamakan daripada hardcode logic spesifik di C++

## Lua Runtime API

Layer Lua harus terus diperluas melalui fungsi runtime yang diekspos dari C++.

Tujuannya:

- memberi script akses ke capability engine/server tanpa memindahkan logic game ke C++
- menjaga gameplay tetap iteratif dan data-driven
- membuat map, NPC, quest, trigger, objective, event, dan live ops lebih mudah dikembangkan

Prinsip desain fungsi yang dapat diakses Lua:

- fungsi harus generik, bukan spesifik untuk satu quest atau satu NPC
- fungsi harus mewakili capability runtime, bukan flow gameplay jadi
- fungsi harus aman dipanggil dari script dan tervalidasi di layer C++
- fungsi harus cukup kecil dan composable agar script bisa membangun behaviour di atasnya

Contoh kategori fungsi yang layak diekspos ke Lua:

- player messaging
- player connection / player count query
- objective update
- map/object/property query
- player state query
- map transfer / teleport
- inventory/economy capability
- entity spawn/despawn
- timer/scheduler hooks
- persistence access
- world event broadcast

Yang harus dihindari:

- builtin yang terlalu spesifik seperti `StartStarterQuestLuna()`
- builtin yang mengandung nama content/map/NPC tertentu
- builtin yang menggabungkan terlalu banyak rule gameplay sekaligus

Tranche builtins awal yang layak diprioritaskan:

- query player connected / player count
- query jumlah player dalam map
- query slot inventory
- set / clear slot inventory
- set / clear player objective

### AD-005: Content harus data-driven dan dapat dipatch dari server

Keputusan:

- Map, metadata, atlas, sprite, dan content pendukung lain diperlakukan sebagai data yang direvisi dan dikirim server.

Alasan:

- memungkinkan perubahan world tanpa shipping ulang client
- cocok untuk MMORPG yang hidup
- mendukung live content workflow

Konsekuensi:

- manifest dan revision system harus dijaga stabil
- cache invalidation harus jelas
- contract asset key/path harus konsisten antar tool, server, dan client

### AD-005A: UI layout dan skin adalah content server-driven

Keputusan:

- UI layout document, theme config, dan atlas image UI diperlakukan sebagai content yang dapat dipatch dari server.

Alasan:

- memungkinkan iterasi visual tanpa rebuild client
- membuat widget runtime reusable untuk lebih dari satu screen/window
- menjaga thin client tetap kecil dan generik

Konsekuensi:

- widget C++ tidak boleh menjadi tempat style final yang keras
- UI framework harus memisahkan `window config`, `theme`, `layer`, dan `widget behaviour`
- atlas UI, theme JSON, dan layout JSON harus punya contract asset key yang konsisten
- fallback hardcoded hanya berfungsi sebagai safety net saat asset UI belum tersedia

### AD-006: Shared contract antara client dan server harus tunggal

Keputusan:

- Opcode, packet meaning, dan kontrak data penting harus dibagi dari definisi shared, bukan diduplikasi liar.

Alasan:

- mencegah drift protocol
- menurunkan risiko bug integrasi
- mempermudah evolusi fitur network

Konsekuensi:

- perubahan protocol harus dievaluasi di dua sisi sekaligus
- format shared perlu dijaga ringkas dan stabil

### AD-007: Tooling adalah authoring pipeline, bukan authority runtime

Keputusan:

- Tiled dan tooling lain diposisikan sebagai alat produksi content, bukan penentu state runtime saat game berjalan.

Alasan:

- memisahkan authoring dari execution
- menjaga runtime tetap sederhana dan jelas

Konsekuensi:

- authoring tool harus menghasilkan output yang dikonsumsi server/client
- authoring tool tidak boleh menjadi sumber state live gameplay

## Arah Struktur Modul

### `nganu.game`

Peran:

- bootstrap client
- patch content
- cache content
- render world
- tampilkan UI
- kirim input ke server
- terima state authoritative

### `nganu.mp`

Peran:

- authoritative multiplayer server
- core runtime C++
- host Lua runtime
- distribusi manifest/content
- session/state management
- plugin integration

### Tiled / Tooling Eksternal

Peran:

- authoring map/content tooling
- memproduksi content yang nantinya disajikan server ke client

Tool authoring bukan runtime authority. Tool authoring hanya untuk content pipeline.

## Target Arsitektur Jangka Menengah

Checklist arah yang harus dicapai:

- shared protocol contract antara server dan client
- movement client-send-position dengan validasi server yang kuat
- reconciliation posisi dari server saat perlu
- content manifest yang stabil
- cache asset yang versioned
- framework UI data-driven dengan layout document, layer system, dan theme atlas
- logic gameplay utama dipindah ke Lua
- penambahan builtins/runtime API untuk Lua secara bertahap
- map/object/NPC/quest behaviour data-driven
- server cleanup session yang kuat saat disconnect graceful maupun non-graceful
- persistence layer untuk state penting
- tooling content yang mendukung patch workflow

## Definisi Selesai Yang Diinginkan

Project dianggap mendekati arah yang benar bila:

- client bootstrap kecil bisa dijalankan sendiri
- client bisa patch content dari server saat start
- server memegang authority gameplay penting
- core server C++ tetap generik
- logic game utama hidup di Lua/data
- perubahan content dan logic tidak menuntut rebuild client
- perubahan gameplay rutin tidak menuntut hardcode baru di C++

## Ringkasan Singkat

`nganu` diarahkan menjadi MMORPG 2D top-down dengan model:

- `small bootstrap client`
- `server-delivered content patching`
- `authoritative multiplayer server`
- `client sends position with server validation`
- `C++ as runtime core`
- `Lua as gameplay/runtime logic layer`
- `Lua-accessible runtime functions as the extension surface`
- `server-driven UI layout and theme assets`

Kalau sebuah fitur baru bisa diselesaikan lewat data atau Lua, jangan hardcode di C++.
Kalau sebuah state penting bisa dimanipulasi client, maka arsitekturnya belum cukup authoritative.
Kalau client harus membawa semua content dari awal, maka arah thin client project ini gagal.
