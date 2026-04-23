# Map Format V2

Format `.map` sekarang **hanya** mendukung `map_format=2`. Format lama tidak didukung parser.

Setiap baris memakai `key=value`. Nilai yang mengandung `,` atau `:` harus di-escape dengan `\`.

## Contoh

```text
map_format=2
map_id=new_map
world_name=New Region
tile_size=32
size=24,18
spawn=400,208

asset=terrain,map,terrain_atlas.png
asset=objects,map,base_out_atlas.png
asset=player,character,base_out_atlas.png

property=music,prototype_day
property=player_sprite_south,player@608@0@32@32

layer=ground,tilemap,asset:terrain@0@0@32@32,tint:#FFFFFFFF,parallax:1
layer=road,tilemap,tint:#FFFFFFFF,parallax:1

tile=ground,7,8,terrain@0@800@32@32
tile=road,11,6,terrain@192@864@32@32
line=road,7,1,9,1,terrain@96@896@32@32
fill=ground,0,0,24,18,terrain@0@800@32@32
tiles=road,terrain@320@832@32@32,17:3,6:8

entity=npc,npc_1,256,64,32,32
prop=npc_1,name,Guide
prop=npc_1,title,NPC
prop=npc_1,sprite,terrain@480@384@64@96

entity=portal,portal_2,416,64,32,32
prop=portal_2,title,Portal
prop=portal_2,sprite,objects@384@608@32@32
prop=portal_2,target_x,416
prop=portal_2,target_y,64
```

## Header

- `map_format=2`
  Wajib. Parser menolak map tanpa versi ini.
- `map_id=<id>`
  Id unik map.
- `world_name=<name>`
  Nama region/world untuk client.
- `tile_size=<pixels>`
  Ukuran tile world dalam pixel.
- `size=<width>,<height>`
  Ukuran map dalam tile.
- `spawn=<x>,<y>`
  Spawn default dalam pixel world.

## Asset Alias

- `asset=<alias>,<domain>,<file>`

`domain` saat ini:

- `map`
- `character`

Alias dipakai oleh atlas ref agar map tidak mengulang nama file panjang.

Contoh:

- `asset=terrain,map,terrain_atlas.png`
- `asset=objects,map,base_out_atlas.png`
- `asset=player,character,base_out_atlas.png`

Atlas ref memakai bentuk:

```text
alias@srcX@srcY@srcW@srcH
```

Contoh:

- `terrain@0@800@32@32`
- `objects@384@0@32@32`
- `player@608@0@32@32`

Parser akan resolve bentuk itu ke domain asset internal seperti `map:terrain_atlas.png@...` atau `character:base_out_atlas.png@...`.

## Property Map

- `property=<key>,<value>`

Contoh:

- `property=music,prototype_day`
- `property=climate,temperate`
- `property=map_script,greenfields_main`
- `property=player_sprite_south,player@608@0@32@32`

`player_sprite_*` boleh memakai alias `character`.

## Layer

- `layer=<name>,<type>[,prop:value...]`

`type` utama sekarang:

- `tilemap`
- `image`
- `color`

Property layer:

- `asset:<atlas-ref>`
- `tint:<#RRGGBB atau #RRGGBBAA>`
- `parallax:<float>`

Contoh:

- `layer=ground,tilemap,asset:terrain@0@0@32@32,tint:#FFFFFFFF,parallax:1`
- `layer=road,tilemap,tint:#FFFFFFFF,parallax:1`

Urutan `layer=` adalah urutan render tile/stamp: layer pertama digambar lebih dulu, layer setelahnya menimpa layer sebelumnya. Editor boleh menambah layer dan menukar urutannya tanpa nama layer khusus.

## Tile

- `tile=<layer>,<x>,<y>,<atlas-ref>`

`x` dan `y` memakai koordinat tile. `atlas-ref` memakai alias dari `asset=`.
Jika source atlas lebih besar dari `tile_size`, stamp tetap dirender memakai ukuran source world-pixel dan anchor bawahnya ditempel ke tile `x,y`; sprite 32x64 tidak di-stretch ke 32x32.

Contoh:

- `tile=ground,7,8,terrain@0@800@32@32`
- `tile=road,11,6,terrain@192@864@32@32`
- `tile=decor,12,9,objects@608@0@32@64`

## Tile Compression

Parser juga mendukung bentuk ringkas yang di-expand menjadi tile runtime biasa:

- `line=<layer>,<x1>,<y1>,<x2>,<y2>,<atlas-ref>`
  Garis horizontal atau vertikal, inclusive.
- `fill=<layer>,<x>,<y>,<width>,<height>,<atlas-ref>`
  Rectangle penuh dalam koordinat tile.
- `tiles=<layer>,<atlas-ref>,<x:y>[,<x:y>...]`
  Banyak koordinat tidak berurutan dengan asset yang sama.

Contoh:

- `line=road,7,1,9,1,terrain@96@896@32@32`
- `fill=ground,0,0,24,18,terrain@0@800@32@32`
- `tiles=road,terrain@320@832@32@32,17:3,6:8`

Gunakan `tile=` hanya untuk satu tile spesifik. Gunakan `line/fill/tiles` untuk map editor output agar ukuran file dan diff tetap kecil.

## Entity

- `entity=<kind>,<id>,<x>,<y>,<width>,<height>`
- `prop=<entity_id>,<key>,<value>`

Jenis entity yang dipakai saat ini:

- `npc`
- `portal`
- `trigger`
- `prop`
- `region`

Property entity yang umum:

- `name`
- `title`
- `description`
- `script`
- `quest`
- `sprite`
- `pivot`
- `facing`
- `z`
- `target_map`
- `target_x`
- `target_y`
- `target_title`
- `collision`
- `collider`

`collision` entity menerima nilai `none` atau `block`. Jika tidak diisi, object bisa mengambil collision dari metadata sprite atlas. `collider` memakai koordinat lokal object dengan format `x|y|width|height`, misalnya `collider=12|22|8|10` untuk hanya menahan gerak di bagian batang bawah.

`width` dan `height` entity adalah ukuran render sprite di world pixel, tidak harus sama dengan `tile_size`. Untuk sprite 32x64 yang ditempatkan pada satu tile 32x32, editor menyimpan bounds 32x64 dan menempatkan dasar sprite pada tile yang diklik supaya sprite tidak gepeng.

## Metadata Atlas

File sidecar atlas tetap didukung:

```text
tile=0,160,32,32,collision:block
tile=608,0,32,64,collision:block,collider:10|52|12|12
```

Sidecar berada di `assets/map_images/<atlas>.atlas`. Metadata ini dipakai untuk collision/tag per tile dan sprite jika tersedia. `collider` memakai koordinat lokal source atlas, lalu runtime menskalakan ke ukuran tile/object yang digambar. Jika sidecar tidak ada, asset tetap valid dan client tidak akan menunggu metadata tersebut.
