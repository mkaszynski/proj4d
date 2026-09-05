#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <optional>
#include <span>

#include "proj4d/math.hpp"
#include "proj4d/world.hpp"

namespace proj4d {

struct BlockCoord3D {
  int x{};
  int y{};
  int z{};

  [[nodiscard]] int &operator[](std::size_t index) {
    return *std::array<int *, 3>{&x, &y, &z}.at(index);
  }
  [[nodiscard]] int operator[](std::size_t index) const {
    return std::array<int, 3>{x, y, z}.at(index);
  }
  [[nodiscard]] auto operator<=>(const BlockCoord3D &) const = default;
};

[[nodiscard]] BlockCoord blockCoord4D(const BlockCoord3D &coordinate);

class BlockWorld3D {
public:
  explicit BlockWorld3D(BlockWorld &world);

  [[nodiscard]] bool isSolid(const BlockCoord3D &coordinate) const;
  [[nodiscard]] bool generatedSolidAt(const BlockCoord3D &coordinate) const;
  [[nodiscard]] bool setSolid(const BlockCoord3D &coordinate, bool solid);
  [[nodiscard]] int surfaceHeightAt(int x, int z) const;
  [[nodiscard]] std::uint32_t seed() const;
  [[nodiscard]] TerrainMode terrainMode() const;
  [[nodiscard]] std::size_t loadedChunkCount() const;
  [[nodiscard]] std::size_t maximumLoadedChunks() const;
  [[nodiscard]] std::uint64_t revision() const;

private:
  BlockWorld &world_;
};

struct RayHit3D {
  BlockCoord3D block{};
  BlockCoord3D placement{};
  double distance{};
  int boundaryNormalAxis{-1};
};

[[nodiscard]] BlockCoord3D containingBlock(const Vec3 &point);
[[nodiscard]] std::optional<RayHit3D> raycast(const BlockWorld3D &world,
                                              const Vec3 &origin,
                                              const Vec3 &direction,
                                              double maximumDistance);
[[nodiscard]] bool breakAlongRay(BlockWorld3D &world, const Vec3 &origin,
                                 const Vec3 &direction, double maximumDistance);
[[nodiscard]] bool buildAlongRay(BlockWorld3D &world, const Vec3 &origin,
                                 const Vec3 &direction, double maximumDistance,
                                 std::span<const BlockCoord3D> protectedBlocks);

} // namespace proj4d
