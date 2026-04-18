# nganu.editor

Editor content ringan berbasis `raylib` untuk membantu memilih source rect atlas, menyusun map `.map`, dan menyiapkan asset yang cocok dengan pipeline `nganu.mp`:

- `map:filename.png@x@y@w@h`
- `character:filename.png@x@y@w@h`

Saat ini editor fokus ke:

- scan atlas dari `../nganu.mp/assets/map_images`
- scan atlas dari `../nganu.mp/assets/characters`
- pilih domain asset `map` atau `character`
- pilih source rect yang snap ke grid
- atur ukuran grid dan ukuran selection
- copy ref atlas ke clipboard
- buka map dari `../nganu.mp/assets/maps`
- paint tile ke layer map aktif
- toggle area `block` dan `water` per tile
- set `spawn`
- save kembali ke format `.map` v2

## Build

```bash
cd nganu.editor
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## Run

```bash
./build/nganu-editor
```

## Controls

- `F1`: mode atlas
- `F2`: mode map

### Atlas Mode

- `Tab`: ganti domain `map` / `character`
- `Q` / `E`: atlas sebelumnya / berikutnya
- `Mouse Wheel`: zoom
- `Right Mouse Drag` / `Middle Mouse Drag`: geser canvas
- `Space` + `Left Drag`: geser canvas
- `WASD`: geser canvas dengan keyboard
- `Left Click`: pilih tile origin
- `Arrow Keys`: geser selection
- `Shift` + `Arrow Keys`: ubah ukuran selection dalam jumlah cell
- `[` / `]`: ubah grid width
- `;` / `'`: ubah grid height
- `1` / `2` / `3` / `4`: preset grid `16 / 24 / 32 / 48`
- `C`: copy ref atlas ke clipboard
- `R`: reset pan/zoom

### Map Mode

- `Ctrl+N`: map baru
- `Ctrl+S`: simpan map
- `PageUp` / `PageDown`: pindah file map
- `1` / `2` / `3` / `4`: pilih layer aktif
- `P`: tool paint stamp
- `X`: tool erase stamp
- `B`: tool blocked
- `V`: tool water
- `G`: tool spawn
- `Mouse Wheel`: zoom map
- `Right Mouse Drag` / `Middle Mouse Drag`: geser canvas map
- `Left Click`: pakai tool aktif di tile

## Catatan

Tool ini masih sederhana, tetapi arahnya adalah editor content umum, bukan hanya atlas picker. Versi ini sudah cukup untuk memilih atlas rect, paint tile map, mengatur area block/water, spawn, object dasar, dan save format `.map` v2.
