#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <optional>
#include <span>

#include "proj4d/math.hpp"
#include "proj4d/world.hpp"

namespace proj4d {

struct BlockCoord2D {
  int x{};
  int y{};

  [[nodiscard]] int &operator[](std::size_t index) {
    return *std::array<int *, 2>{&x, &y}.at(index);
  }
  [[nodiscard]] int operator[](std::size_t index) const {
    return std::array<int, 2>{x, y}.at(index);
  }
  [[nodiscard]] auto operator<=>(const BlockCoord2D &) const = default;
};

[[nodiscard]] BlockCoord blockCoord4D(const BlockCoord2D &coordinate);

class BlockWorld2D {
public:
  explicit BlockWorld2D(BlockWorld &world);

  [[nodiscard]] bool isSolid(const BlockCoord2D &coordinate) const;
  [[nodiscard]] bool generatedSolidAt(const BlockCoord2D &coordinate) const;
  [[nodiscard]] bool setSolid(const BlockCoord2D &coordinate, bool solid);
  [[nodiscard]] int surfaceHeightAt(int x) const;
  [[nodiscard]] std::uint32_t seed() const;
  [[nodiscard]] TerrainMode terrainMode() const;
  [[nodiscard]] std::size_t loadedChunkCount() const;
  [[nodiscard]] std::size_t maximumLoadedChunks() const;
  [[nodiscard]] std::uint64_t revision() const;

private:
  BlockWorld &world_;
};

struct RayHit2D {
  BlockCoord2D block{};
  BlockCoord2D placement{};
  double distance{};
  int boundaryNormalAxis{-1};
};

[[nodiscard]] BlockCoord2D containingBlock(const Vec2 &point);
[[nodiscard]] std::optional<RayHit2D> raycast(const BlockWorld2D &world,
                                              const Vec2 &origin,
                                              const Vec2 &direction,
                                              double maximumDistance);
[[nodiscard]] bool breakAlongRay(BlockWorld2D &world, const Vec2 &origin,
                                 const Vec2 &direction, double maximumDistance);
[[nodiscard]] bool buildAlongRay(BlockWorld2D &world, const Vec2 &origin,
                                 const Vec2 &direction, double maximumDistance,
                                 std::span<const BlockCoord2D> protectedBlocks);

} // namespace proj4d
