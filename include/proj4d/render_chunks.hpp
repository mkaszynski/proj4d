#pragma once

#include <vector>

#include "proj4d/coordinates.hpp"

namespace proj4d {

[[nodiscard]] std::vector<ChunkCoord>
planRenderChunks2D(const ChunkCoord &center, int radius);

[[nodiscard]] std::vector<ChunkCoord>
planRenderChunks3D(const ChunkCoord &center, int radius);

} // namespace proj4d
