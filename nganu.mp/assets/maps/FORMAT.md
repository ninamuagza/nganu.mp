# Map Format V2

Format `.map` sekarang **hanya** mendukung `map_format=2`. Format lama seperti `width=`, `height=`, `stamp=`, `object=`, `blocked=`, dan `water=` sudah tidak didukung parser.

Setiap baris memakai `key=value`. Nilai yang mengandung `,` atau `:` harus di-escape dengan `\`.

## Contoh

```text
map_format=2
map_id=new_map
world_name=New Region
tile_size=48
size=24,18
spawn=600,312

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

area=block,672,336,48,48
area=water,920,120,300,210

entity=npc,npc_1,384,96,48,48
prop=npc_1,name,Guide
prop=npc_1,title,NPC
prop=npc_1,sprite,terrain@480@384@64@96

entity=portal,portal_2,624,96,48,48
prop=portal_2,title,Portal
prop=portal_2,sprite,objects@384@608@32@32
prop=portal_2,target_x,624
prop=portal_2,target_y,96
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

Property layer:

- `asset:<atlas-ref>`
- `tint:<#RRGGBB atau #RRGGBBAA>`
- `parallax:<float>`

Contoh:

- `layer=ground,tilemap,asset:terrain@0@0@32@32,tint:#FFFFFFFF,parallax:1`
- `layer=road,tilemap,tint:#FFFFFFFF,parallax:1`

## Tile

- `tile=<layer>,<x>,<y>,<atlas-ref>`

`x` dan `y` memakai koordinat tile. `atlas-ref` memakai alias dari `asset=`.

Contoh:

- `tile=ground,7,8,terrain@0@800@32@32`
- `tile=road,11,6,terrain@192@864@32@32`

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

## Area

- `area=<kind>,<x>,<y>,<width>,<height>`

`kind` saat ini:

- `block`
- `water`

Area memakai koordinat pixel world. Area ini masuk ke server/client collision.

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

## Metadata Atlas

File sidecar atlas tetap didukung:

```text
tile=0,160,32,32,collision:block,tag:water
```

Sidecar berada di `assets/map_images/<atlas>.atlas`. Metadata ini dipakai untuk collision/tag per tile jika tersedia. Jika sidecar tidak ada, asset tetap valid dan client tidak akan menunggu metadata tersebut.
