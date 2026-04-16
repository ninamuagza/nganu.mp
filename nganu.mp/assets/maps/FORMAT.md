# Map Format

Format `.map` sekarang dinaikkan supaya cukup untuk jangka panjang: visual layer, property, spawn, collision, dan object dasar. Format ini sengaja tetap text supaya mudah dipakai server, client, dan nanti map editor.

## Bentuk File

Setiap baris memakai `key=value`.

Contoh:

```text
map_id=starter_field
world_name=Greenfields
tile=48
width=42
height=30
spawn=160,640
property=music,greenfields_day
layer=ground,color,palette:grassland,#CDE6D8FF,1.0
object=npc,luna,304,624,32,32,title:Field Guide,behavior:starter_guide
blocked=260,220,220,140
water=920,120,300,210
```

## Header Dasar

- `map_id`
  Id map unik.
- `world_name`
  Nama region/world yang tampil di client.
- `tile`
  Ukuran tile dasar dalam pixel.
- `width`
  Lebar map dalam tile.
- `height`
  Tinggi map dalam tile.
- `spawn=x,y`
  Titik spawn default map.

## Property

- `property=key,value`
  Metadata bebas untuk map.

Contoh:

- `property=music,greenfields_day`
- `property=climate,temperate`
- `property=biome,starter_plains`
- `property=map_script,greenfields_main`

Ini disiapkan untuk sifat map seperti:

- ambience
- weather
- bgm
- region flags
- rules tertentu

## Layer

- `layer=name,kind,asset,tint,parallax`

Kolom:

- `name`
  Nama layer, misalnya `ground`, `road`, `detail`.
- `kind`
  Jenis layer. Saat ini minimal:
  - `color`
  - `image`
- `asset`
  Referensi asset yang nanti bisa di-resolve dari server asset pipeline.
  Untuk atlas sederhana saat ini bisa pakai bentuk:
  `filename@srcX@srcY@srcW@srcH`
- `tint`
  Warna `#RRGGBB` atau `#RRGGBBAA`.
- `parallax`
  Faktor parallax.

Contoh:

- `layer=ground,image,map:terrain_atlas.png@0@0@32@32,#FFFFFFFF,1.0`
- `layer=road,image,map:terrain_atlas.png@96@0@32@32,#FFFFFFFF,1.0`

## Object

- `object=kind,id,x,y,width,height[,prop:value ...]`

Object adalah entitas dasar yang diletakkan di map.

Contoh:

- `object=npc,luna,304,624,32,32,title:Field Guide,behavior:starter_guide`
- `object=trigger,starter_road_marker,850,590,180,120,quest:starter_scout_road`
- `object=portal,waygate_crossroads,1184,640,96,96,target_x:2544,target_y:624`
- `object=prop,sign_west,256,612,32,32,title:Road Sign,sprite:map:base_out_atlas.png@384@0@32@32`
- `object=region,greenfields_region,0,384,1728,864,title:Greenfields,climate:temperate`

Jenis object yang layak dipakai nanti:

- `npc`
- `trigger`
- `spawn`
- `portal`
- `prop`
- `region`

Property object yang mulai dipakai sekarang:

- `title`
- `description`
- `behavior`
- `script`
- `quest`
- `sprite`
- `pivot`
- `facing`
- `z`
- `intro1`
- `intro2`
- `progress`
- `complete`
- `idle`
- `complete_text`
- `turnin_text`
- `target_map`
- `target_x`
- `target_y`
- `target_title`

Nilai `sprite` memakai format atlas yang sama seperti layer image:

- `sprite=filename@srcX@srcY@srcW@srcH`

- `script=handler_name`
  Nama handler Lua yang akan dipakai untuk object atau map-level behavior.

## Stamp

- `stamp=layer,x,y,asset`

Stamp adalah tile paint per-cell yang ditulis editor map. `x` dan `y` memakai koordinat tile, bukan pixel.

Contoh:

- `stamp=ground,5,8,map:terrain_atlas.png@0@0@32@32`
- `stamp=road,6,8,map:terrain_atlas.png@96@0@32@32`
- `stamp=detail,10,12,map:base_out_atlas.png@320@0@32@32`

Ini dipakai untuk map editor supaya tile visual tidak lagi harus hardcoded sebagai satu sprite untuk seluruh layer.

Stamp sekarang juga bisa membawa collision/logic dari metadata atlas. Jadi tile seperti air, duri, lumpur, atau lava tidak perlu punya key map khusus seperti `water=...`.

Contoh metadata atlas sidecar:

```text
tile=0,160,32,32,collision:block,tag:water
```

File sidecar diletakkan di folder atlas map image dengan nama `<atlas>.atlas`, misalnya:

- `assets/map_images/base_out_atlas.atlas`

Property tambahan:

- `pivot=x|y`
  Titik origin sprite dalam rasio bounds object.
  Contoh `pivot:0.5|1.0`.
- `facing`
  Arah object seperti `north`, `east`, `south`, `west`, atau derajat numerik.
- `z`
  Z-layer sederhana untuk draw order object.
- `target_map`
  Opsional untuk `portal`. Kalau diisi dan beda dengan map aktif, portal akan memindahkan player ke map lain.
- `target_x`, `target_y`
  Posisi tujuan untuk `portal`. Ini sekarang jalur utama untuk teleport di map yang sama.
- `target_title`
  Label tujuan teleport untuk feedback UI/server text.

## Area Berulang

- `blocked=x,y,width,height`
  Area collision padat.
- `water=x,y,width,height`
  Area non-walkable atau terrain khusus.

Catatan:

- `blocked` dan `water` masih didukung sebagai format lama.
- Arah baru yang disarankan adalah memakai `stamp` + metadata atlas agar collision/terrain tag ikut tile yang dipaint, bukan area hardcoded terpisah.

## Arah Jangka Panjang

Format ini sengaja dijaga linear dan mudah ditulis editor.

Pipeline yang dituju:

1. map editor menulis `.map`
2. server membaca `.map`
3. server mengirim manifest + asset yang relevan
4. client parse hasilnya dan render/map logic mengikuti data itu

Ke depan format ini bisa ditambah tanpa mematahkan bentuk dasarnya, misalnya:

- `tileset=id,asset,tilew,tileh`
- `nav=...`
- `light=...`
- `sound=...`
- `script=...`
