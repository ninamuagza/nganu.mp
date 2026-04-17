# LUA API

Dokumen ini merangkum fungsi runtime yang saat ini diekspos dari core C++ ke Lua.

Prinsip umum:

- Fungsi di bawah ini adalah capability runtime, bukan flow gameplay jadi.
- Script Lua bertugas menyusun rule gameplay dari fungsi-fungsi ini.
- Builtin harus dipakai secara generik, bukan diikat ke satu map/NPC tertentu.

## Messaging

`print(...)`

- Menulis log ke logger server.

`SendPlayerMessage(playerid, text)`

- Mengirim pesan server ke satu player.

`BroadcastMessage(text)`

- Broadcast pesan ke semua player.

`BroadcastMapMessage(map_id, text)`

- Broadcast pesan hanya ke player di map tertentu.

## Player State

`GetPlayerName(playerid) -> string`

`SetPlayerName(playerid, name) -> bool`

`IsPlayerConnected(playerid) -> bool`

`GetPlayerCount() -> integer`

`GetMapPlayerCount(map_id) -> integer`

`GetPlayerPosition(playerid) -> x, y`

`SetPlayerSpawnPosition(playerid, x, y)`

- Saat ini mengubah posisi server-side player.

`TeleportPlayer(playerid, x, y[, reason]) -> bool`

- Teleport intra-map yang tetap melewati validasi server.

`TransferPlayerMap(playerid, map_id[, x, y]) -> bool`

- Transfer player ke map lain.
- Builtin ini adalah jalur utama untuk rule portal/travel yang ditulis di Lua.

## Text / Command Context

`GetLastPlayerText() -> string`

- Berguna untuk command parser atau reaction terhadap chat.

`GetTickCount() -> integer`

- Mengembalikan jumlah milidetik sejak runtime Lua diinisialisasi.

## Map and Property Query

`GetMapId() -> string`

`GetPlayerMapId(playerid) -> string`

`GetMapProperty(key) -> string|nil`

`GetPlayerMapProperty(playerid, key) -> string|nil`

## Map Object Query

`GetMapObjectId(object_index) -> string|nil`

`GetPlayerMapObjectId(playerid, object_index) -> string|nil`

`GetMapObjectKind(object_index) -> string|nil`

`GetPlayerMapObjectKind(playerid, object_index) -> string|nil`

`GetMapObjectProperty(object_index, key) -> string|nil`

`GetPlayerMapObjectProperty(playerid, object_index, key) -> string|nil`

`GetPlayerMapObjectBounds(playerid, object_index) -> x, y, width, height`

`GetPlayerMapObjectCenter(playerid, object_index) -> center_x, center_y`

## Objective

`SetPlayerObjective(playerid, text)`

- Mengirim objective text ke client.

`ClearPlayerObjective(playerid)`

- Mengosongkan objective text client.

## Inventory

`GetInventorySlot(playerid, slot_index) -> item_def_id, amount, flags, occupied`

`CountInventoryItem(playerid, item_def_id) -> integer`

`FindInventorySlot(playerid, item_def_id) -> slot_index`

- Mengembalikan `-1` bila tidak ditemukan.

`FindFreeInventorySlot(playerid) -> slot_index`

- Mengembalikan `-1` bila penuh.

`SetInventorySlot(playerid, slot_index, item_def_id, amount[, flags])`

`ClearInventorySlot(playerid, slot_index)`

`AddInventoryItem(playerid, item_def_id, amount[, flags]) -> bool`

- Menambah item dan merge stack bila memungkinkan.

`RemoveInventoryItem(playerid, item_def_id, amount) -> bool`

- Mengurangi item lintas stack bila total cukup.

## Design Notes

Saat butuh fungsi baru:

- utamakan builtin yang generik
- jangan buat builtin yang menyebut nama quest/map/NPC tertentu
- pikirkan apakah fungsi itu capability runtime atau justru flow gameplay yang seharusnya tetap di Lua

Contoh yang baik:

- `SpawnWorldItem(...)`
- `CountPlayersNearObject(...)`
- `HasQuestStage(...)`

Contoh yang buruk:

- `StartLunaQuest(...)`
- `OpenCrossroadsGate(...)`
- `GiveStarterRoadReward(...)`
