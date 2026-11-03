#pragma once

#include "hypercraft/world/chunk.hpp"
#include "hypercraft/world/ore_registry.hpp"

#include <cstddef>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string_view>

namespace hypercraft {

struct BiomeDefinition;
class BiomeRegistry;
class ContentRegistry;
class OreRegistry;
struct TreeDefinition;
class TreeRegistry;

enum class WorldGenerationType : std::uint8_t {
    normal = 0,
    flat = 1,
    high = 2,
    overhang = 3,
};

constexpr int world_generation_type_count = 4;
const char* world_generation_type_label(WorldGenerationType type);
double world_generation_diminishing_multiplier(WorldGenerationType type);
bool valid_world_generation_type(std::uint8_t value);

enum class Biome {
    plains,
    forest,
    rainforest,
    desert,
    taiga,
    tundra,
};

struct TerrainGenerationCacheStats {
    std::size_t bytes = 0;
    std::size_t entries = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    std::uint64_t coalesced_waits = 0;
    std::size_t tree_candidate_tile_bytes = 0;
    std::size_t tree_candidate_tile_entries = 0;
    std::uint64_t tree_candidate_tile_hits = 0;
    std::uint64_t tree_candidate_tile_misses = 0;
};

class TerrainChunkGeneration {
public:
    struct promise_type;
    using Handle = std::coroutine_handle<promise_type>;

    TerrainChunkGeneration() = default;
    TerrainChunkGeneration(TerrainChunkGeneration&& other) noexcept;
    TerrainChunkGeneration& operator=(TerrainChunkGeneration&& other) noexcept;
    ~TerrainChunkGeneration();

    TerrainChunkGeneration(const TerrainChunkGeneration&) = delete;
    TerrainChunkGeneration& operator=(const TerrainChunkGeneration&) = delete;

    bool advance();
    bool done() const;
    Chunk take_chunk();

    struct promise_type {
        TerrainChunkGeneration get_return_object();
        std::suspend_always initial_suspend() const noexcept;
        std::suspend_always final_suspend() const noexcept;
        std::suspend_always yield_value(std::size_t) const noexcept;
        void return_value(Chunk chunk);
        void unhandled_exception() noexcept;

        std::optional<Chunk> result;
        std::exception_ptr error;
    };

private:
    explicit TerrainChunkGeneration(Handle handle);

    Handle handle_ {};
};

class TerrainGenerator {
public:
    TerrainGenerator();
    explicit TerrainGenerator(std::uint32_t seed);
    TerrainGenerator(std::uint32_t seed, WorldGenerationType generation_type);
    TerrainGenerator(
        std::uint32_t seed,
        WorldGenerationType generation_type,
        const ContentRegistry& content_registry,
        const BiomeRegistry& biome_registry,
        const TreeRegistry& tree_registry,
        const OreRegistry& ore_registry = OreRegistry::default_registry());

    Chunk generate_chunk(ChunkCoord coord) const;
    TerrainChunkGeneration start_chunk_generation(ChunkCoord coord) const;
    int height_at(int x, int z) const;
    int height_at(int x, int z, int w) const;
    Biome biome_at(int x, int z, int w) const;
    std::string_view biome_id_at(int x, int z, int w) const;
    bool cave_carves_at(BlockCoord coord) const;
    BlockId generated_block_at(BlockCoord coord) const;
    TerrainGenerationCacheStats cache_stats() const;

private:
    struct CacheState;
    struct CachedOrePatch;

    bool has_ocean_fill() const;
    bool content_is_solid(BlockId block) const;
    const BiomeDefinition& biome_definition_at(int x, int z, int w) const;
    BlockId base_block_at(BlockCoord coord) const;
    BlockId base_block_at(BlockCoord coord, const BiomeDefinition& biome) const;
    BlockId classify_raw_solid_block_at(BlockCoord coord, const BiomeDefinition& biome) const;
    BlockId air_or_fluid_at(BlockCoord coord) const;
    BlockId fluid_block_for_biome(const BiomeDefinition& biome, int y) const;
    BlockId surface_block_for_biome(const BiomeDefinition& biome, int y) const;
    BlockId filler_block_for_biome(const BiomeDefinition& biome, int depth, int y) const;
    bool surface_can_support_tree(
        BlockCoord coord,
        const BiomeDefinition& biome,
        const TreeDefinition& tree) const;
    BlockId ore_patch_block_at(BlockCoord coord) const;
    std::shared_ptr<const CachedOrePatch> ore_patch_for_cell(
        const OreGenerationRule& rule,
        int cell_x,
        int cell_y,
        int cell_z,
        int cell_w) const;
    void stamp_ore_candidates(ChunkCoord coord, std::array<BlockId, chunk_volume>& candidates) const;
    bool is_buried_ore_position(BlockCoord coord) const;
    std::uint32_t tree_chance_roll(BlockCoord coord) const;
    std::uint32_t max_tree_chance() const;
    int tree_height_at(BlockCoord base, const TreeDefinition& tree) const;
    int tree_leaf_radius_squared(BlockCoord base, const TreeDefinition& tree) const;
    BlockCoord tree_trunk_coord_at(BlockCoord base, int level, const TreeDefinition& tree) const;
    bool tree_canopy_contains(
        const TreeDefinition& tree,
        BlockCoord top,
        int leaf_radius_squared,
        BlockCoord coord) const;
    double height_guide_at(int x, int z, int w) const;
    int height_at_4d(int x, int z, int w) const;
    double cave_noise_height(BlockCoord coord, int noise_index) const;
    double perlin_noise(double x, double y, double z, std::uint32_t salt) const;
    double perlin_noise_4d(double x, double y, double z, double w, std::uint32_t salt) const;
    double fractal_noise(double x, double y, double z) const;
    double fractal_noise_4d(double x, double y, double z, double w) const;
    double density_at_with_height_guide(BlockCoord coord, double base_height) const;
    double density_at(BlockCoord coord) const;
    bool raw_generated_solid_with_height_guide(BlockCoord coord, double base_height) const;
    bool raw_generated_solid(BlockCoord coord) const;
    bool is_generated_solid(BlockCoord coord) const;

    std::uint32_t seed_;
    WorldGenerationType generation_type_ = WorldGenerationType::normal;
    const ContentRegistry* content_registry_ = nullptr;
    const BiomeRegistry* biome_registry_ = nullptr;
    const TreeRegistry* tree_registry_ = nullptr;
    const OreRegistry* ore_registry_ = nullptr;
    std::shared_ptr<CacheState> cache_state_;
};

} // namespace hypercraft
