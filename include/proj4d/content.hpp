#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "hypercraft/world/block.hpp"

namespace proj4d {

using BlockId = hypercraft::BlockId;

struct BlockColor {
  std::uint8_t red{};
  std::uint8_t green{};
  std::uint8_t blue{};

  [[nodiscard]] constexpr std::array<std::uint8_t, 3> array() const {
    return {red, green, blue};
  }
};

inline constexpr BlockColor skyColor{92, 157, 232};

[[nodiscard]] bool isSolid(BlockId block);
[[nodiscard]] bool isKnownBlock(BlockId block);
[[nodiscard]] bool isRenderable(BlockId block);
[[nodiscard]] bool isPlaceable(BlockId block);
[[nodiscard]] bool isStorable(BlockId block);
[[nodiscard]] bool isTool(BlockId block);
[[nodiscard]] int maximumStackSize(BlockId block);
[[nodiscard]] int maximumDurability(BlockId block);
[[nodiscard]] double toolSpeed(BlockId block);
[[nodiscard]] double miningMultiplier(BlockId block);
[[nodiscard]] std::string_view blockName(BlockId block);
[[nodiscard]] BlockColor blockColor(BlockId block);
[[nodiscard]] BlockColor shadedBlockColor(BlockId block, int percent);
[[nodiscard]] bool isOre(BlockId block);

} // namespace proj4d
