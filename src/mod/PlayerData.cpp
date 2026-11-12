#include "mod/PlayerData.h"

#include <array>
#include <set>
#include <string>
#include <string_view>

#include "mc/deps/nbt/ListTag.h"
#include "mc/deps/nbt/StringTag.h"

namespace invlink {

namespace {
// Whole entries copied as they are (items keep every detail: enchantments, names, shulker contents, ...).
constexpr std::array<std::string_view, 7> kKeys{
    "Inventory", "Armor", "Offhand", "EnderChestInventory", "PlayerLevel", "PlayerLevelProgress", "ActiveEffects"};

// Attributes: only these are shared; movement speed, luck and the like stay per server.
std::set<std::string, std::less<>> const kAttributes{
    "minecraft:health",
    "minecraft:absorption",
    "minecraft:player.hunger",
    "minecraft:player.saturation",
    "minecraft:player.exhaustion",
    "minecraft:player.level",
    "minecraft:player.experience",
};

std::string attributeName(Tag const& tag) {
    if (tag.getId() != Tag::Type::Compound) return {};
    auto const& c = static_cast<CompoundTag const&>(tag);
    if (!c.contains("Name", Tag::Type::String)) return {};
    return c.at("Name").get<StringTag>();
}
} // namespace

CompoundTag extractShared(CompoundTag const& full) {
    CompoundTag out;
    for (auto key : kKeys) {
        if (full.contains(key)) out[key] = full.at(key);
    }
    if (full.contains("Attributes", Tag::Type::List)) {
        ListTag shared;
        for (auto const& entry : full.at("Attributes").get<ListTag>()) {
            if (entry && kAttributes.contains(attributeName(*entry))) shared.push_back(entry); // copies
        }
        out["Attributes"] = std::move(shared);
    }
    return out;
}

void mergeShared(CompoundTag& full, CompoundTag const& shared) {
    for (auto key : kKeys) {
        if (shared.contains(key)) {
            full[key] = shared.at(key);
        } else if (key == "ActiveEffects") {
            full.erase(key); // no effects on the shared side: none here either
        }
    }
    if (shared.contains("Attributes", Tag::Type::List)) {
        ListTag merged;
        if (full.contains("Attributes", Tag::Type::List)) {
            for (auto const& entry : full.at("Attributes").get<ListTag>()) {
                if (entry && !kAttributes.contains(attributeName(*entry))) merged.push_back(entry);
            }
        }
        for (auto const& entry : shared.at("Attributes").get<ListTag>()) {
            if (entry) merged.push_back(entry);
        }
        full["Attributes"] = std::move(merged);
    }
}

} // namespace invlink
