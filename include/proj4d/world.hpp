#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "proj4d/math.hpp"

namespace proj4d {

struct BlockCoord {
  int x{};
  int y{};
  int z{};
  int w{};

  [[nodiscard]] int &operator[](std::size_t index) {
    return *std::array<int *, 4>{&x, &y, &z, &w}.at(index);
  }
  [[nodiscard]] int operator[](std::size_t index) const {
    return std::array<int, 4>{x, y, z, w}.at(index);
  }
  [[nodiscard]] auto operator<=>(const BlockCoord &) const = default;
};

struct WorldBounds {
  BlockCoord minimum{-4, -3, -4, -4};
  BlockCoord maximum{4, 4, 4, 4};
};

struct BoundaryCell {
  BlockCoord block{};
  int axis{};
  int side{};
};

struct RayHit {
  BlockCoord block{};
  BlockCoord placement{};
  double distance{};
};

class BlockWorld {
public:
  explicit BlockWorld(WorldBounds bounds = {});
  void fillFlatGround();
  [[nodiscard]] bool inBounds(const BlockCoord &coordinate) const;
  [[nodiscard]] bool isSolid(const BlockCoord &coordinate) const;
  [[nodiscard]] bool setSolid(const BlockCoord &coordinate, bool solid);
  [[nodiscard]] std::size_t solidCount() const;
  [[nodiscard]] const WorldBounds &bounds() const;
  [[nodiscard]] std::vector<BoundaryCell> exposedBoundaryCells() const;

private:
  [[nodiscard]] std::size_t indexOf(const BlockCoord &coordinate) const;
  WorldBounds bounds_{};
  std::array<int, 4> extents_{};
  std::vector<bool> blocks_{};
};

[[nodiscard]] BlockCoord containingBlock(const Vec4 &point);
[[nodiscard]] std::optional<RayHit> raycast(const BlockWorld &world,
                                            const Vec4 &origin,
                                            const Vec4 &direction,
                                            double maximumDistance);
[[nodiscard]] bool breakAlongRay(BlockWorld &world, const Vec4 &origin,
                                 const Vec4 &direction, double maximumDistance);
[[nodiscard]] bool buildAlongRay(BlockWorld &world, const Vec4 &origin,
                                 const Vec4 &direction, double maximumDistance,
                                 std::span<const BlockCoord> protectedBlocks);

} // namespace proj4d
