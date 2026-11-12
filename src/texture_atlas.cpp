#include "proj4d/texture_atlas.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "hypercraft/render/texture_atlas.hpp"

namespace proj4d {
namespace {

#include "pickaxe_texture_data.inc"

struct OwnedAtlas {
  std::vector<std::uint8_t> rgba;
  MaterialTextureAtlas view;
};

int tileFor(BlockId material) {
  if (material == BlockId::air) {
    material = BlockId::grass;
  }
  return std::max(0, static_cast<int>(material) - 1);
}

void replaceTile(std::vector<std::uint8_t> &atlas, int atlasWidth,
                 BlockId material,
                 const std::array<std::uint8_t, 1024> &replacement) {
  const int tileX = tileFor(material) * materialTextureSize;
  for (int y = 0; y < materialTextureSize; ++y) {
    const std::size_t source =
        static_cast<std::size_t>(y * materialTextureSize * 4);
    const std::size_t destination =
        static_cast<std::size_t>((y * atlasWidth + tileX) * 4);
    std::copy_n(replacement.begin() + static_cast<std::ptrdiff_t>(source),
                materialTextureSize * 4,
                atlas.begin() + static_cast<std::ptrdiff_t>(destination));
  }
}

OwnedAtlas buildAtlas() {
  hypercraft::TextureAtlas source = hypercraft::create_block_texture_atlas();
  replaceTile(source.rgba, source.width, BlockId::wood_tool, wood_tool_rgba);
  replaceTile(source.rgba, source.width, BlockId::stone_tool, stone_tool_rgba);
  replaceTile(source.rgba, source.width, BlockId::tin_tool, tin_tool_rgba);
  replaceTile(source.rgba, source.width, BlockId::copper_tool,
              copper_tool_rgba);
  replaceTile(source.rgba, source.width, BlockId::iron_tool, iron_tool_rgba);
  OwnedAtlas atlas;
  atlas.rgba = std::move(source.rgba);
  atlas.view = {source.width, source.height, atlas.rgba};
  return atlas;
}

const OwnedAtlas &ownedAtlas() {
  static const OwnedAtlas atlas = buildAtlas();
  return atlas;
}

int texel(double coordinate) {
  coordinate = std::clamp(coordinate, 0.0, std::nextafter(1.0, 0.0));
  return std::clamp(static_cast<int>(coordinate * materialTextureSize), 0,
                    materialTextureSize - 1);
}

std::uint8_t averageChannel(std::uint8_t first, std::uint8_t second,
                            std::uint8_t third) {
  return static_cast<std::uint8_t>(
      (static_cast<int>(first) + second + third + 1) / 3);
}

} // namespace

const MaterialTextureAtlas &materialTextureAtlas() { return ownedAtlas().view; }

int materialTextureTile(BlockId material) { return tileFor(material); }

TexturePixel materialTexturePixel(BlockId material, int x, int y) {
  const MaterialTextureAtlas &atlas = materialTextureAtlas();
  x = std::clamp(x, 0, materialTextureSize - 1);
  y = std::clamp(y, 0, materialTextureSize - 1);
  const int atlasX = materialTextureTile(material) * materialTextureSize + x;
  const std::size_t index =
      static_cast<std::size_t>((y * atlas.width + atlasX) * 4);
  return {atlas.rgba[index], atlas.rgba[index + 1U], atlas.rgba[index + 2U],
          atlas.rgba[index + 3U]};
}

TexturePixel sampleMaterialTexture2D(BlockId material, double u, double v) {
  return materialTexturePixel(material, texel(u), texel(v));
}

TexturePixel sampleMaterialTexture1D(BlockId material, double t,
                                     int tangentAxis) {
  return tangentAxis == 0 ? sampleMaterialTexture2D(material, t, 0.5)
                          : sampleMaterialTexture2D(material, 0.5, t);
}

TexturePixel sampleMaterialTexture3D(BlockId material, double u, double v,
                                     double w) {
  // A direction-neutral volumetric uplift of Hypercraft's square tile. Each
  // voxel blends the three orthogonal square samples, so no 4D face grants a
  // preferred texture direction and every 2D source feature contributes.
  const TexturePixel xy = sampleMaterialTexture2D(material, u, v);
  const TexturePixel yz = sampleMaterialTexture2D(material, v, w);
  const TexturePixel zx = sampleMaterialTexture2D(material, w, u);
  return {averageChannel(xy.red, yz.red, zx.red),
          averageChannel(xy.green, yz.green, zx.green),
          averageChannel(xy.blue, yz.blue, zx.blue),
          averageChannel(xy.alpha, yz.alpha, zx.alpha)};
}

} // namespace proj4d
