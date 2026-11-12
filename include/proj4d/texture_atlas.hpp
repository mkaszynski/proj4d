#pragma once

#include <cstdint>
#include <span>

#include "proj4d/content.hpp"

namespace proj4d {

inline constexpr int materialTextureSize = 16;

struct TexturePixel {
  std::uint8_t red{};
  std::uint8_t green{};
  std::uint8_t blue{};
  std::uint8_t alpha{};

  [[nodiscard]] auto operator<=>(const TexturePixel &) const = default;
};

struct MaterialTextureAtlas {
  int width{};
  int height{};
  std::span<const std::uint8_t> rgba;
};

[[nodiscard]] const MaterialTextureAtlas &materialTextureAtlas();
[[nodiscard]] int materialTextureTile(BlockId material);
[[nodiscard]] TexturePixel materialTexturePixel(BlockId material, int x, int y);
[[nodiscard]] TexturePixel sampleMaterialTexture2D(BlockId material, double u,
                                                   double v);
[[nodiscard]] TexturePixel sampleMaterialTexture1D(BlockId material, double t,
                                                   int tangentAxis);
[[nodiscard]] TexturePixel sampleMaterialTexture3D(BlockId material, double u,
                                                   double v, double w);

} // namespace proj4d
