# Map Format TMX

Source map repo sekarang memakai file `.tmx` dengan schema custom `nganu_format=tmx_v1`.
Map diauthor lewat Tiled, lalu pipeline server dan client membaca file `.tmx` ini langsung.

## Ringkasan

- Root file adalah `<map ...>` TMX ortogonal biasa.
- Metadata map disimpan di `<map><properties>`.
- Setiap layer runtime disimpan sebagai `<objectgroup>` dengan property `nganu_role=map_layer`.
- Object gameplay disimpan di `<objectgroup name="objects">` dengan property `nganu_role=map_objects`.
- Stamp tile/sprite di dalam layer disimpan sebagai `<object type="stamp">`.

## Property Map

Property wajib di root:

- `nganu_format=tmx_v1`
- `map_id=<id>`
- `world_name=<nama>`
- `spawn_x=<float>`
- `spawn_y=<float>`

Property map gameplay lain tetap disimpan sebagai property biasa, misalnya:

- `music`
- `climate`
- `biome`
- `map_script`
- `player_sprite_north`
- `player_sprite_east`
- `player_sprite_west`
- `player_sprite_south`

Atlas ref tetap memakai format lama:

```text
map:terrain_atlas.png@0@800@32@32
character:base_out_atlas.png@608@0@32@32
```

## Layer Runtime

Setiap layer runtime disimpan sebagai:

```xml
<objectgroup name="ground">
  <properties>
    <property name="nganu_role" value="map_layer"/>
    <property name="kind" value="tilemap"/>
    <property name="asset" value="map:terrain_atlas.png@0@0@32@32"/>
    <property name="tint" value="#FFFFFFFF"/>
    <property name="parallax" type="float" value="1"/>
  </properties>
  ...
</objectgroup>
```

Property layer:

- `kind`: `tilemap`, `image`, atau `color`
- `asset`: optional, dipakai untuk layer image/repeat
- `tint`
- `parallax`

Urutan `objectgroup` dengan `nganu_role=map_layer` adalah urutan render layer.

## Stamp Layer

Setiap stamp disimpan sebagai object TMX:

```xml
<object name="stamp" type="stamp" x="320" y="160" width="32" height="32">
  <properties>
    <property name="asset_ref" value="map:terrain_atlas.png@0@800@32@32"/>
    <property name="grid_x" type="int" value="10"/>
    <property name="grid_y" type="int" value="5"/>
  </properties>
</object>
```

Property stamp:

- `asset_ref`
- `grid_x`
- `grid_y`

`x/y/width/height` menyimpan bounds world aktual sprite. `grid_x/grid_y` dipakai untuk menjaga snapping tile tetap stabil.

## Object Gameplay

Object gameplay disimpan di group:

```xml
<objectgroup name="objects">
  <properties>
    <property name="nganu_role" value="map_objects"/>
  </properties>
  ...
</objectgroup>
```

Contoh object:

```xml
<object name="portal_to_crossroads" type="portal" x="789" y="427" width="64" height="64">
  <properties>
    <property name="title" value="Road to Crossroads"/>
    <property name="script" value="portal_travel"/>
    <property name="sprite" value="map:base_out_atlas.png@384@0@32@32"/>
    <property name="target_map" value="crossroads"/>
    <property name="target_x" value="235"/>
    <property name="target_y" value="416"/>
  </properties>
</object>
```

`name` adalah object id runtime, `type` adalah kind runtime (`npc`, `portal`, `trigger`, `prop`, `region`), dan semua property gameplay tetap dipertahankan apa adanya.

## Catatan

- Repo sudah pindah ke `.tmx`; server tidak lagi menscan `.map`, dan authoring map dilakukan lewat Tiled.
- Parser shared masih bisa membaca text map lama untuk kebutuhan konversi satu arah, tetapi source aktif repo adalah `.tmx`.
