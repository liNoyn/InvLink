#include "mod/LiveItems.h"

#include "mc/deps/nbt/ListTag.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlayerHotbarPacket.h"
#include "mc/world/ContainerID.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/Container.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

namespace invlink {

namespace {
using ArmorSlot = SharedTypes::Legacy::ArmorSlot;

CompoundTag const* compound(UniqueTagPtr const& entry) {
    return entry && entry->getId() == Tag::Type::Compound ? static_cast<CompoundTag const*>(entry.get()) : nullptr;
}

int slotOf(CompoundTag const& item) {
    if (!item.contains("Slot")) return -1;
    auto const& v = item.at("Slot");
    if (v.hold(Tag::Type::Byte)) return static_cast<unsigned char>(v.get<ByteTag>().data);
    if (v.hold(Tag::Type::Int)) return v.get<IntTag>().data;
    return -1;
}

void fillBySlot(Container& container, CompoundTag const& shared, std::string_view key) {
    container.removeAllItems();
    if (!shared.contains(key, Tag::Type::List)) return;
    int i = 0;
    for (auto const& entry : shared.at(key).get<ListTag>()) {
        auto const* item = compound(entry);
        int         slot = item ? slotOf(*item) : -1;
        if (slot < 0) slot = i; // lists without "Slot" are in slot order
        ++i;
        if (item && slot < container.getContainerSize()) container.setItem(slot, ItemStack::fromTag(*item));
    }
}
} // namespace

void applyItems(Player& player, CompoundTag const& shared) {
    fillBySlot(player.getInventory(), shared, "Inventory");
    if (auto ender = player.getEnderChestContainer()) fillBySlot(*ender, shared, "EnderChestInventory");
    if (shared.contains("Armor", Tag::Type::List)) {
        int i = 0;
        for (auto const& entry : shared.at("Armor").get<ListTag>()) {
            if (i >= static_cast<int>(ArmorSlot::HumanoidCount)) break;
            auto const* item = compound(entry);
            player.setArmor(static_cast<ArmorSlot>(i++), item ? ItemStack::fromTag(*item) : ItemStack::EMPTY_ITEM());
        }
    }
    if (shared.contains("Offhand", Tag::Type::List)) {
        auto const& list = shared.at("Offhand").get<ListTag>();
        auto const* item = list.empty() ? nullptr : compound(list.front());
        player.setOffhandSlot(item ? ItemStack::fromTag(*item) : ItemStack::EMPTY_ITEM());
    }
    if (shared.contains("SelectedInventorySlot")) {
        auto const& v    = shared.at("SelectedInventorySlot");
        int         slot = v.hold(Tag::Type::Int) ? v.get<IntTag>().data
                         : v.hold(Tag::Type::Byte) ? static_cast<unsigned char>(v.get<ByteTag>().data)
                                                   : -1;
        if (slot >= 0 && slot < 9) {
            player.setSelectedSlot(slot);
            if (auto packet = std::static_pointer_cast<PlayerHotbarPacket>(
                    MinecraftPackets::createPacket(MinecraftPacketIds::PlayerHotbar)
                )) {
                packet->mSelectedSlot     = static_cast<uint>(slot);
                packet->mShouldSelectSlot = true;
                packet->mContainerId      = ContainerID::Inventory;
                packet->sendTo(player);
            }
        }
    }
    player.sendInventory(true);
}

} // namespace invlink
