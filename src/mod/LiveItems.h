#pragma once
// Items of a player who is already in the world. Loading a tag replaces the inventory but not what is worn,
// held in the offhand or kept in the ender chest, so those go through their containers here.

#include "mc/deps/nbt/CompoundTag.h"

class Player;

namespace invlink {

/** Puts the shared tag's items in place (the same four places), replacing what was there. */
void applyItems(Player& player, CompoundTag const& shared);

} // namespace invlink
