#include "mod/PlayerData.h"

#include <array>
#include <set>
#include <string>
#include <string_view>

#include "mc/deps/nbt/ListTag.h"
#include "mc/deps/nbt/StringTag.h"

namespace invlink {

namespace {
// Whole entries copied as they are (items keep every detail: enchantments, names, shulker contents, ...):
// inventory 0-35, armour (helmet, chestplate, leggings, boots), offhand, the selected hotbar slot, ender chest 0-26,
// and the experience (level + progress, which together are the total XP).
constexpr std::array<std::string_view, 7> kKeys{
    "Inventory",
    "Armor",
    "Offhand",
    "SelectedInventorySlot",
    "EnderChestInventory",
    "PlayerLevel",
    "PlayerLevelProgress",
};

// Attributes: only the experience is shared; health, hunger, effects, movement speed and the like stay per server.
std::set<std::string, std::less<>> const kAttributes{
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
        if (shared.contains(key)) full[key] = shared.at(key);
    }
    if (shared.contains("Attributes", Tag::Type::List)) {
        ListTag merged;
        if (full.contains("Attributes", Tag::Type::List)) {
            for (auto const& entry : full.at("Attributes").get<ListTag>()) {
                if (entry && !kAttributes.contains(attributeName(*entry))) merged.push_back(entry);
            }
        }
        for (auto const& entry : shared.at("Attributes").get<ListTag>()) {
            // records saved by older versions also hold health, hunger, ...: those stay per server now
            if (entry && kAttributes.contains(attributeName(*entry))) merged.push_back(entry);
        }
        full["Attributes"] = std::move(merged);
    }
}

} // namespace invlink
