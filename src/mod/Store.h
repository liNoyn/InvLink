#pragma once
// The shared player-data folder of the linked servers (no Minecraft types).
//
//   <dir>/players/<uuid>.json          the newest shared data of one player and who owns it now
//   <dir>/players/<uuid>.history/      older versions (newest kept, oldest removed), for recovery by hand
//   <dir>/alive/<server>               last heartbeat of a server, milliseconds
//   <dir>/backup/<server>/<uuid>.nbt   what this server had before it first took the shared data (kept, never overwritten)
//
// A record is owned by at most one server while the player is there ("owner"); it is released (owner "") when the
// player leaves, so the next server can take it. Every write goes to a temporary file first and is renamed over the
// old one, so a reader never sees half a file.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace invlink {

struct Record {
    std::uint64_t version = 0;
    std::string   owner;          // "" = nobody (free to take)
    std::int64_t  ownerSince = 0; // ms
    std::int64_t  savedAt    = 0; // ms
    std::string   savedBy;
    std::string   name;
    std::string   data;           // binary NBT (little endian) of the shared part, raw bytes
};

class Store {
public:
    explicit Store(std::filesystem::path dir, std::string server, int keepHistory = 20);

    [[nodiscard]] std::filesystem::path const& dir() const { return mDir; }
    [[nodiscard]] std::string const&           server() const { return mServer; }

    /** The record, or nullopt when there is none. Throws std::runtime_error when the file exists but is broken. */
    std::optional<Record> read(std::string const& uuid) const;
    /** Writes the record whole (and keeps the previous version in the history). */
    void write(std::string const& uuid, Record const& record) const;

    /** Keeps this server's own copy from before sharing, once per player. False when one is already kept. */
    bool keepFirstBackup(std::string const& uuid, std::string const& binaryNbt) const;

    void heartbeat(std::int64_t nowMs) const;
    /** True when that server wrote a heartbeat within timeoutMs. */
    bool alive(std::string const& server, std::int64_t nowMs, std::int64_t timeoutMs) const;

    static std::string base64(std::string const& raw);
    static std::string unbase64(std::string const& text);

private:
    std::filesystem::path recordFile(std::string const& uuid) const;
    void                  writeWhole(std::filesystem::path const& file, std::string const& content) const;

    std::filesystem::path mDir;
    std::string           mServer;
    int                   mKeepHistory;
};

} // namespace invlink
