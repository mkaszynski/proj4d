#include "hypercraft/world/coordinates.hpp"

#include <cstdint>

namespace hypercraft {
namespace {

std::size_t fold_hash(std::uint64_t hash)
{
    if constexpr (sizeof(std::size_t) >= sizeof(std::uint64_t)) {
        return static_cast<std::size_t>(hash);
    } else {
        return static_cast<std::size_t>(hash ^ (hash >> 32U));
    }
}

void mix_coord(std::uint64_t& hash, int value)
{
    hash ^= static_cast<std::uint64_t>(static_cast<std::int64_t>(value) + 0x9e3779b9ll);
    hash *= 1099511628211ull;
}

} // namespace

std::size_t ChunkCoordHash::operator()(ChunkCoord coord) const
{
    std::uint64_t hash = 1469598103934665603ull;
    mix_coord(hash, coord.x);
    mix_coord(hash, coord.y);
    mix_coord(hash, coord.z);
    mix_coord(hash, coord.w);
    return fold_hash(hash);
}

std::size_t BlockCoordHash::operator()(BlockCoord coord) const
{
    std::uint64_t hash = 1469598103934665603ull;
    mix_coord(hash, coord.x);
    mix_coord(hash, coord.y);
    mix_coord(hash, coord.z);
    mix_coord(hash, coord.w);
    return fold_hash(hash);
}

std::size_t RenderSliceIdentityHash::operator()(const RenderSliceIdentity& identity) const
{
    std::uint64_t hash = static_cast<std::uint64_t>(ChunkCoordHash {}(identity.coord));
    mix_coord(hash, static_cast<int>(identity.slice));
    mix_coord(hash, identity.hidden_block);
    return fold_hash(hash);
}

bool operator<(ChunkCoord left, ChunkCoord right)
{
    if (left.x != right.x) {
        return left.x < right.x;
    }
    if (left.y != right.y) {
        return left.y < right.y;
    }
    if (left.z != right.z) {
        return left.z < right.z;
    }
    return left.w < right.w;
}

bool operator<(BlockCoord left, BlockCoord right)
{
    if (left.x != right.x) {
        return left.x < right.x;
    }
    if (left.y != right.y) {
        return left.y < right.y;
    }
    if (left.z != right.z) {
        return left.z < right.z;
    }
    return left.w < right.w;
}

int floor_div(int value, int divisor)
{
    int result = value / divisor;
    const int remainder = value % divisor;
    if (remainder != 0 && ((remainder < 0) != (divisor < 0))) {
        --result;
    }
    return result;
}

int floor_mod(int value, int divisor)
{
    const int result = value % divisor;
    return result < 0 ? result + divisor : result;
}

ChunkCoord chunk_coord_for_block(BlockCoord block)
{
    return {
        floor_div(block.x, chunk_size),
        floor_div(block.y, chunk_size),
        floor_div(block.z, chunk_size),
        floor_div(block.w, chunk_size),
    };
}

LocalBlockCoord local_coord_for_block(BlockCoord block)
{
    return {
        floor_mod(block.x, chunk_size),
        floor_mod(block.y, chunk_size),
        floor_mod(block.z, chunk_size),
        floor_mod(block.w, chunk_size),
    };
}

ChunkBlockCoord split_block_coord(BlockCoord block)
{
    return {chunk_coord_for_block(block), local_coord_for_block(block)};
}

BlockCoord block_coord_from_chunk_local(ChunkCoord chunk, LocalBlockCoord local)
{
    return {
        chunk.x * chunk_size + local.x,
        chunk.y * chunk_size + local.y,
        chunk.z * chunk_size + local.z,
        chunk.w * chunk_size + local.w,
    };
}

std::size_t local_block_index(LocalBlockCoord local)
{
    return static_cast<std::size_t>(local.x + chunk_size * (local.z + chunk_size * (local.y + chunk_size * local.w)));
}

SliceView next_slice_view(SliceView view)
{
    switch (view) {
    case SliceView::xyz:
        return SliceView::xyw;
    case SliceView::xyw:
        return SliceView::yzw;
    case SliceView::yzw:
        return SliceView::xzw;
    case SliceView::xzw:
        return SliceView::xyz;
    }
    return SliceView::xyz;
}

Axis4 hidden_axis(SliceView view)
{
    switch (view) {
    case SliceView::xyz:
        return Axis4::w;
    case SliceView::xyw:
        return Axis4::z;
    case SliceView::yzw:
        return Axis4::x;
    case SliceView::xzw:
        return Axis4::y;
    }
    return Axis4::w;
}

std::array<Axis4, 3> visible_axes(SliceView view)
{
    switch (view) {
    case SliceView::xyz:
        return {Axis4::x, Axis4::y, Axis4::z};
    case SliceView::xyw:
        return {Axis4::x, Axis4::y, Axis4::w};
    case SliceView::yzw:
        return {Axis4::z, Axis4::y, Axis4::w};
    case SliceView::xzw:
        return {Axis4::x, Axis4::z, Axis4::w};
    }
    return {Axis4::x, Axis4::y, Axis4::z};
}

int coord_value(BlockCoord coord, Axis4 axis)
{
    switch (axis) {
    case Axis4::x:
        return coord.x;
    case Axis4::y:
        return coord.y;
    case Axis4::z:
        return coord.z;
    case Axis4::w:
        return coord.w;
    }
    return 0;
}

int coord_value(ChunkCoord coord, Axis4 axis)
{
    switch (axis) {
    case Axis4::x:
        return coord.x;
    case Axis4::y:
        return coord.y;
    case Axis4::z:
        return coord.z;
    case Axis4::w:
        return coord.w;
    }
    return 0;
}

int coord_value(LocalBlockCoord coord, Axis4 axis)
{
    switch (axis) {
    case Axis4::x:
        return coord.x;
    case Axis4::y:
        return coord.y;
    case Axis4::z:
        return coord.z;
    case Axis4::w:
        return coord.w;
    }
    return 0;
}

BlockCoord with_coord_value(BlockCoord coord, Axis4 axis, int value)
{
    switch (axis) {
    case Axis4::x:
        coord.x = value;
        break;
    case Axis4::y:
        coord.y = value;
        break;
    case Axis4::z:
        coord.z = value;
        break;
    case Axis4::w:
        coord.w = value;
        break;
    }
    return coord;
}

ChunkCoord with_coord_value(ChunkCoord coord, Axis4 axis, int value)
{
    switch (axis) {
    case Axis4::x:
        coord.x = value;
        break;
    case Axis4::y:
        coord.y = value;
        break;
    case Axis4::z:
        coord.z = value;
        break;
    case Axis4::w:
        coord.w = value;
        break;
    }
    return coord;
}

LocalBlockCoord with_coord_value(LocalBlockCoord coord, Axis4 axis, int value)
{
    switch (axis) {
    case Axis4::x:
        coord.x = value;
        break;
    case Axis4::y:
        coord.y = value;
        break;
    case Axis4::z:
        coord.z = value;
        break;
    case Axis4::w:
        coord.w = value;
        break;
    }
    return coord;
}

} // namespace hypercraft
