# Protocol and Asset Flow (Ringkas)

Dokumen ini merangkum kontrak runtime inti antara `nganu.mp` (server) dan `nganu.game` (client), berdasarkan `shared/Protocol.h`.

## Opcode utama

### Packet opcode
- `HANDSHAKE (0x01)`
- `PLAYER_DATA (0x02)`
- `GAME_STATE (0x03)`
- `PLAYER_MOVE (0x04)`
- `OBJECT_INTERACT (0x05)`
- `PLUGIN_MESSAGE (0x10)`
- `CHAT_MESSAGE (0x11)`

### Game state subtype
- `SNAPSHOT (0x01)`
- `PLAYER_JOIN (0x02)`
- `PLAYER_LEAVE (0x03)`
- `SERVER_TEXT (0x04)`
- `PLAYER_NAME (0x05)`
- `OBJECTIVE_TEXT (0x06)`
- `MAP_TRANSFER (0x07)`
- `PLAYER_POSITION (0x08)`

### Plugin message subtype
- `PLAYER_NAME (0x01)`
- `UPDATE_PROBE (0x20)`
- `UPDATE_MANIFEST (0x21)`
- `ASSET_REQUEST (0x22)`
- `ASSET_BLOB (0x23)`

## Alur koneksi dan bootstrap konten

```text
Client connect
  -> Server kirim HANDSHAKE(playerId)
  -> Server kirim GAME_STATE snapshot awal
  -> Client kirim PLUGIN_MESSAGE: UPDATE_PROBE
  -> Server kirim PLUGIN_MESSAGE: UPDATE_MANIFEST
       (server_name, content_revision, world_name, map_id, asset=...)
  -> Client pilih asset yang belum ada di cache
  -> Client kirim PLUGIN_MESSAGE: ASSET_REQUEST(assetKey)
  -> Server kirim PLUGIN_MESSAGE: ASSET_BLOB(key, kind, revision, encoding, content)
  -> Client simpan cache + apply map/world
```

## Alur transfer map

```text
Runtime trigger/portal di server
  -> Server kirim GAME_STATE: MAP_TRANSFER(mapId, spawnX, spawnY)
  -> Client mulai bootstrap map baru (cek cache, request bila perlu)
  -> Client apply map baru dan lanjut gameplay
```

## Kontrak stabilitas

- Jangan ubah nilai enum opcode/subtype tanpa update serentak server + client.
- Perubahan format `UPDATE_MANIFEST` dan `ASSET_BLOB` harus backward-compatible atau disertai bump `content_revision`.
- Referensi aset map/character harus konsisten dengan domain:
  - `map_image:*`
  - `map_meta:*`
  - `character_image:*`

