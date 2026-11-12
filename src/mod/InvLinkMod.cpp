// InvLink: shares inventory, armour, offhand, the selected hotbar slot, ender chest and experience between linked
// LeviLamina servers through a shared folder. See README.md.
//
// - Joining: the hook on LevelStorage::loadServerPlayerData puts the newest shared data into the data BDS just read
//   from its own world, before the player is built, and this server takes ownership of the record.
// - While here: saved every saveSeconds (crash safety), ownership kept.
// - Leaving (quit, kick, transfer to another server): saved and released at once (owner "").
// - A record still owned by another server that is alive (it has not saved yet) is not taken: the player is asked to
//   join again in a few seconds. If that server stopped, the last save is used.

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/event/server/ServerStoppingEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"
#include "ll/api/service/Bedrock.h"
#include "mc/dataloadhelper/DefaultDataLoadHelper.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/storage/LevelStorage.h"

#include "mod/LiveItems.h"
#include "mod/Messages.h"
#include "mod/PlayerData.h"
#include "mod/Store.h"

namespace invlink {

namespace {
std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}
} // namespace

class InvLinkMod {
public:
    static InvLinkMod& getInstance() {
        static auto* instance = new InvLinkMod;
        return *instance;
    }
    InvLinkMod() : mSelf(*ll::mod::NativeMod::current()) {}
    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    bool load();
    bool enable();
    bool disable();

    std::unique_ptr<CompoundTag> onLoad(Player const& client, std::unique_ptr<CompoundTag> tag);

private:
    struct Config {
        std::string server;
        std::string store            = "../../../.invlink";
        std::string language         = "en";
        std::string kickMessage;     // "" = the built-in text in `language`
        int         saveSeconds      = 60;
        int         aliveTimeoutSecs = 30;
        int         keepHistory      = 20;
    };

    template <class... Args>
    std::string say(Msg msg, Args&&... args) const {
        return fmt::format(fmt::runtime(text(mLang, msg)), std::forward<Args>(args)...);
    }

    void onJoin(Player& player);
    void onLeave(Player& player);
    void onTick();
    void saveOwned(Player& player, bool release);
    void releaseAll();
    bool readConfig();

    ll::mod::NativeMod&                          mSelf;
    Config                                       mConfig;
    Lang                                         mLang = Lang::En;
    std::unique_ptr<Store>                       mStore;
    std::set<std::string>                        mOwned;   // uuids whose record this server owns
    std::set<std::string>                        mBlocked; // uuids that may not stay (record held elsewhere, or broken)
    std::unordered_map<std::string, std::string> mPending; // uuid -> shared NBT to apply after joining
    std::set<std::string>                        mNew;     // uuids without any record yet (first time anywhere)
    ll::event::ListenerPtr                       mJoin, mLeave, mTick, mStopping;
    std::uint64_t                                mTicks = 0;
    DefaultDataLoadHelper                        mHelper;
};

} // namespace invlink

// ------------------------------------------------------------------ the load hook

LL_TYPE_INSTANCE_HOOK(
    InvLinkLoadPlayerHook,
    ll::memory::HookPriority::Normal,
    LevelStorage,
    &LevelStorage::loadServerPlayerData,
    std::unique_ptr<CompoundTag>,
    Player const& client,
    bool          isXboxLive
) {
    auto tag = origin(client, isXboxLive);
    return invlink::InvLinkMod::getInstance().onLoad(client, std::move(tag));
}

namespace invlink {

bool InvLinkMod::readConfig() {
    auto file = getSelf().getModDir() / "config.json";
    auto& log = getSelf().getLogger();
    if (!std::filesystem::exists(file)) {
        nlohmann::json j{
            {"server", ""},
            {"store", mConfig.store},
            {"language", mConfig.language},
            {"kickMessage", ""},
            {"saveSeconds", mConfig.saveSeconds},
            {"aliveTimeoutSeconds", mConfig.aliveTimeoutSecs},
            {"keepHistory", mConfig.keepHistory}
        };
        std::ofstream(file) << j.dump(2) << "\n";
        log.warn("{}", say(Msg::ConfigCreated));
        return false;
    }
    try {
        std::ifstream in(file);
        auto          j          = nlohmann::json::parse(in);
        mConfig.server           = j.value("server", "");
        mConfig.store            = j.value("store", mConfig.store);
        mConfig.language         = j.value("language", mConfig.language);
        mConfig.kickMessage      = j.value("kickMessage", "");
        mConfig.saveSeconds      = std::max(10, j.value("saveSeconds", mConfig.saveSeconds));
        mConfig.aliveTimeoutSecs = std::max(10, j.value("aliveTimeoutSeconds", mConfig.aliveTimeoutSecs));
        mConfig.keepHistory      = std::max(0, j.value("keepHistory", mConfig.keepHistory));
        mLang                    = parseLang(mConfig.language);
    } catch (std::exception const& e) {
        log.error("{}", say(Msg::ConfigBroken, e.what()));
        return false;
    }
    if (mConfig.server.empty()) {
        log.warn("{}", say(Msg::ServerEmpty));
        return false;
    }
    return true;
}

bool InvLinkMod::load() {
    if (!readConfig()) return true; // loaded but inactive
    try {
        std::filesystem::path dir = mConfig.store;
        if (dir.is_relative()) dir = getSelf().getModDir() / dir;
        mStore = std::make_unique<Store>(std::filesystem::weakly_canonical(dir), mConfig.server, mConfig.keepHistory);
    } catch (std::exception const& e) {
        getSelf().getLogger().error("{}", say(Msg::StoreUnusable, e.what()));
        return true;
    }
    InvLinkLoadPlayerHook::hook();
    getSelf().getLogger().info("{}", say(Msg::Sharing, mConfig.server, mStore->dir().string()));
    return true;
}

bool InvLinkMod::enable() {
    if (!mStore) return true;
    auto& bus = ll::event::EventBus::getInstance();
    mJoin = bus.emplaceListener<ll::event::PlayerJoinEvent>([this](ll::event::PlayerJoinEvent& e) { onJoin(e.self()); });
    mLeave = bus.emplaceListener<ll::event::PlayerDisconnectEvent>([this](ll::event::PlayerDisconnectEvent& e) {
        onLeave(e.self());
    });
    mTick     = bus.emplaceListener<ll::event::ServerLevelTickEvent>([this](ll::event::ServerLevelTickEvent&) { onTick(); });
    mStopping = bus.emplaceListener<ll::event::ServerStoppingEvent>([this](ll::event::ServerStoppingEvent&) { releaseAll(); });
    mStore->heartbeat(nowMs());
    return true;
}

bool InvLinkMod::disable() {
    if (mStore) releaseAll();
    mJoin.reset();
    mLeave.reset();
    mTick.reset();
    mStopping.reset();
    return true;
}

std::unique_ptr<CompoundTag> InvLinkMod::onLoad(Player const& client, std::unique_ptr<CompoundTag> tag) {
    if (!mStore) return tag;
    std::string uuid = client.getUuid().asString();
    std::string name = client.getRealName();
    auto&       log  = getSelf().getLogger();
    try {
        auto record = mStore->read(uuid);
        auto now    = nowMs();
        if (record && !record->owner.empty() && record->owner != mConfig.server
            && mStore->alive(record->owner, now, mConfig.aliveTimeoutSecs * 1000LL)) {
            mBlocked.insert(uuid);
            log.warn("{}", say(Msg::StillOwned, name, record->owner));
            return tag;
        }
        if (record && !record->owner.empty() && record->owner != mConfig.server) {
            log.warn("{}", say(Msg::OwnerStopped, name, record->owner, record->version));
        }
        if (record) {
            auto shared = CompoundTag::fromBinaryNbt(record->data);
            if (!shared) throw std::runtime_error("broken shared data");
            if (tag) {
                // the first time this server's own items are replaced by the shared ones: keep them
                if (mStore->keepFirstBackup(uuid, extractShared(*tag).toBinaryNbt())) {
                    log.info("{}", say(Msg::BackedUp, name, mConfig.server));
                }
                mergeShared(*tag, *shared);
            } else {
                mPending[uuid] = record->data; // first time on this server: applied once the player exists
            }
            record->owner      = mConfig.server;
            record->ownerSince = now;
            mStore->write(uuid, *record);
            log.info("{}", say(Msg::Loaded, name, record->version, record->savedBy));
        } else if (tag) {
            mStore->keepFirstBackup(uuid, extractShared(*tag).toBinaryNbt());
            Record fresh{1, mConfig.server, now, now, mConfig.server, name, extractShared(*tag).toBinaryNbt()};
            mStore->write(uuid, fresh);
            log.info("{}", say(Msg::StartedHere, name));
        } else {
            mNew.insert(uuid); // brand new everywhere: recorded once the player exists
        }
        mOwned.insert(uuid);
    } catch (std::exception const& e) {
        mBlocked.insert(uuid);
        log.error("{}", say(Msg::ReadFailed, name, e.what()));
    }
    return tag;
}

void InvLinkMod::onJoin(Player& player) {
    std::string uuid = player.getUuid().asString();
    if (mBlocked.erase(uuid)) {
        player.disconnect(mConfig.kickMessage.empty() ? std::string(text(mLang, Msg::Kick)) : mConfig.kickMessage);
        return;
    }
    if (auto it = mPending.find(uuid); it != mPending.end()) {
        auto shared = CompoundTag::fromBinaryNbt(it->second);
        mPending.erase(it);
        if (shared) {
            CompoundTag full;
            player.save(full);
            mergeShared(full, *shared);
            player.load(full, mHelper);
            applyItems(player, *shared);
        }
    }
    if (mNew.erase(uuid)) {
        CompoundTag full;
        player.save(full);
        auto now = nowMs();
        mStore->write(uuid, Record{1, mConfig.server, now, now, mConfig.server, player.getRealName(),
                                   extractShared(full).toBinaryNbt()});
    }
}

void InvLinkMod::saveOwned(Player& player, bool release) {
    std::string uuid = player.getUuid().asString();
    if (!mOwned.contains(uuid)) return;
    try {
        auto        record = mStore->read(uuid);
        CompoundTag full;
        player.save(full);
        auto now = nowMs();
        Record next{
            record ? record->version + 1 : 1,
            release ? "" : mConfig.server,
            release ? 0 : (record ? record->ownerSince : now),
            now,
            mConfig.server,
            player.getRealName(),
            extractShared(full).toBinaryNbt()
        };
        mStore->write(uuid, next);
        if (release) mOwned.erase(uuid);
    } catch (std::exception const& e) {
        getSelf().getLogger().error("{}", say(Msg::SaveFailed, player.getRealName(), e.what()));
    }
}

void InvLinkMod::onLeave(Player& player) {
    std::string uuid = player.getUuid().asString();
    mPending.erase(uuid);
    mNew.erase(uuid);
    saveOwned(player, true);
}

void InvLinkMod::onTick() {
    ++mTicks;
    if (mTicks % 100 == 0) {
        try {
            mStore->heartbeat(nowMs());
        } catch (...) {
        }
    }
    if (mTicks % (static_cast<std::uint64_t>(mConfig.saveSeconds) * 20) == 0) {
        if (auto level = ll::service::getLevel()) {
            level->forEachPlayer([&](Player& p) {
                saveOwned(p, false);
                return true;
            });
        }
    }
}

void InvLinkMod::releaseAll() {
    if (auto level = ll::service::getLevel()) {
        level->forEachPlayer([&](Player& p) {
            saveOwned(p, true);
            return true;
        });
    }
}

} // namespace invlink

LL_REGISTER_MOD(invlink::InvLinkMod, invlink::InvLinkMod::getInstance());
