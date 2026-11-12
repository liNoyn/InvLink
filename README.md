# InvLink

[English](README.md) | [日本語](README.ja.md) | [简体中文](README.zh-CN.md)

A [LeviLamina](https://github.com/LiteLDev/LeviLamina) mod that shares player data between several Bedrock Dedicated Servers, so a player keeps the same inventory on every linked server.
No proxy plugin is needed: the servers only share a folder.

## What is shared

```
Shared
├─ Inventory 0-35
├─ Helmet
├─ Chestplate
├─ Leggings
├─ Boots
├─ Offhand
├─ Selected hotbar slot
├─ Ender Chest 0-26
└─ Experience
   └─ Total XP
```

Items are copied as they are: enchantments, names, durability, shulker box contents and everything else.
Everything else stays per server: position, dimension, spawn point, game mode, permissions, health, hunger, potion effects, recipes and so on.

## How it works

- Every linked server points at the same **shared folder**. For each player it holds the newest shared data (the original Minecraft NBT, nothing converted) and which server **owns** it right now.
- **Joining**: InvLink hooks the moment BDS reads the player's data from its world (`LevelStorage::loadServerPlayerData`) and puts the shared data in before the player is created, so the client receives the right inventory from the start. The server becomes the owner.
- **Playing**: saved every `saveSeconds` (default 60) in case the server crashes.
- **Leaving** (quit, kick, `/transfer` to another server, …): saved and released at once, so the next server can take it.
- **Stopping the server**: everyone online is saved and released when the `stop` command runs. If the server stops any other way (crash, killed process), the last regular save is used.
- **Never duplicated**: while another server still owns the data and is running, a player who joins is asked to wait a few seconds and join again. If that server stopped without releasing, the last save is used (it may go back a little, never duplicate).
- **Backups**: when a server first replaces a player's own items with the shared ones, it keeps its old copy once in `backup/<server>/<uuid>.nbt`. Older versions of the shared data are kept in `players/<uuid>.history/` (`keepHistory`, default 20).
- Every file is written to a temporary file first and then renamed, so a reader never sees half a file.

## Requirements

- Bedrock Dedicated Server with LeviLamina **26.60.0** (BDS 1.26.60.x). Build against the version your servers run.
- All linked servers must be able to reach the shared folder (same machine, or a network folder that supports rename).
- The linked servers should run the same add-ons, so that every item exists on every server.

## Install

1. Copy `InvLink` (the built mod folder) to `mods/` of every linked server.
2. Start the server once. `mods/InvLink/config.json` is created.
3. Set `server` to a unique name for this server, and `store` to the shared folder (the same on every server). Restart.

```json
{
  "server": "s1",
  "store": "../../../.invlink",
  "language": "en",
  "kickMessage": "",
  "saveSeconds": 60,
  "aliveTimeoutSeconds": 30,
  "keepHistory": 20
}
```

| Key | Meaning |
|---|---|
| `server` | This server's name, unique among the linked servers. Empty: InvLink does nothing. |
| `store` | The shared folder. A relative path is relative to the mod folder. |
| `language` | `en`, `ja` or `zh`, for the log and the message shown to players. |
| `kickMessage` | Message shown when a player must wait (data still owned by another server). Empty: the built-in text. |
| `saveSeconds` | How often online players are saved (minimum 10). |
| `aliveTimeoutSeconds` | A server without a heartbeat for this long counts as stopped (minimum 10). |
| `keepHistory` | How many old versions of the shared data are kept per player. |

**Before enabling it on a live network, back up the worlds of every linked server.**

## Build

Needs [xmake](https://xmake.io) and clang-cl (LLVM) with the MSVC build tools.

```bash
xmake repo -u
xmake f -p windows -a x64 -m release -y --target_type=server
xmake
```

The mod is written to `bin/InvLink/`.

## Tested

On two local servers (BDS 1.26.60.6 + LeviLamina 26.60.0) with a test client and a real client:

- inventory, all four armour pieces, offhand, ender chest and experience carried over in both directions; health and effects were not in the shared data
- leaving one server and joining the other at once: saved on leaving, loaded on joining
- joining the second server while still online on the first: refused with the message, no duplicate
- the first-time backup was written

## License

[MIT](LICENSE)
