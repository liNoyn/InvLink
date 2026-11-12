#pragma once
// Which parts of a player's saved data are shared between the linked servers, and how they are swapped in.
// Everything else (position, dimension, spawn point, game mode, permissions, recipes, ...) stays per server.

#include "mc/deps/nbt/CompoundTag.h"

namespace invlink {

/** The shared part of a player's full tag (Player::save / the world database). */
CompoundTag extractShared(CompoundTag const& full);

/** Puts the shared part into a full tag. Attributes are merged by name: only the shared ones are replaced. */
void mergeShared(CompoundTag& full, CompoundTag const& shared);

} // namespace invlink
