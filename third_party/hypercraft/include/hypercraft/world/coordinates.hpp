#pragma once

#include <array>
#include <cstddef>

namespace hypercraft {

constexpr int chunk_size = 16;
constexpr int chunk_volume = chunk_size * chunk_size * chunk_size * chunk_size;

struct BlockCoord {
    int x = 0;
    int y = 0;
    int z = 0;
    int w = 0;

    friend constexpr bool operator==(BlockCoord left, BlockCoord right) = default;
};

struct ChunkCoord {
    int x = 0;
    int y = 0;
    int z = 0;
    int w = 0;

    friend constexpr bool operator==(ChunkCoord left, ChunkCoord right) = default;
};

struct ChunkCoordHash {
    std::size_t operator()(ChunkCoord coord) const;
};

struct BlockCoordHash {
    std::size_t operator()(BlockCoord coord) const;
};

struct LocalBlockCoord {
    int x = 0;
    int y = 0;
    int z = 0;
    int w = 0;

    friend constexpr bool operator==(LocalBlockCoord left, LocalBlockCoord right) = default;
};

struct ChunkBlockCoord {
    ChunkCoord chunk;
    LocalBlockCoord local;
};

enum class Axis4 {
    x,
    y,
    z,
    w,
};

enum class SliceView {
    xyz,
    xyw,
    yzw,
    xzw,
};

struct RenderSliceIdentity {
    ChunkCoord coord {};
    SliceView slice = SliceView::xyz;
    int hidden_block = 0;

    friend bool operator==(const RenderSliceIdentity&, const RenderSliceIdentity&) = default;
};

struct RenderSliceIdentityHash {
    std::size_t operator()(const RenderSliceIdentity& identity) const;
};

bool operator<(ChunkCoord left, ChunkCoord right);
bool operator<(BlockCoord left, BlockCoord right);
int floor_div(int value, int divisor);
int floor_mod(int value, int divisor);
ChunkCoord chunk_coord_for_block(BlockCoord block);
LocalBlockCoord local_coord_for_block(BlockCoord block);
ChunkBlockCoord split_block_coord(BlockCoord block);
BlockCoord block_coord_from_chunk_local(ChunkCoord chunk, LocalBlockCoord local);
std::size_t local_block_index(LocalBlockCoord local);
SliceView next_slice_view(SliceView view);
Axis4 hidden_axis(SliceView view);
std::array<Axis4, 3> visible_axes(SliceView view);
int coord_value(BlockCoord coord, Axis4 axis);
int coord_value(ChunkCoord coord, Axis4 axis);
int coord_value(LocalBlockCoord coord, Axis4 axis);
BlockCoord with_coord_value(BlockCoord coord, Axis4 axis, int value);
ChunkCoord with_coord_value(ChunkCoord coord, Axis4 axis, int value);
LocalBlockCoord with_coord_value(LocalBlockCoord coord, Axis4 axis, int value);

} // namespace hypercraft
