#pragma once

#include "hypercraft/world/coordinates.hpp"
#include "hypercraft/world/terrain_generator.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hypercraft {

enum class ChunkGenerationStage : std::size_t {
    biome_lookup,
    density_and_caves,
    ore_patches,
    block_classification,
    tree_candidates,
    tree_application,
    count,
};

enum class ChunkMeshStage : std::size_t {
    chunk_lookup,
    face_extraction,
    count,
};

struct ChunkGenerationCounters {
    std::uint64_t perlin_3d_calls = 0;
    std::uint64_t fractal_3d_calls = 0;
    std::uint64_t perlin_4d_calls = 0;
    std::uint64_t fractal_4d_calls = 0;
    std::uint64_t height_guide_calls = 0;
    std::uint64_t density_calls = 0;
    std::uint64_t raw_solid_queries = 0;
    std::uint64_t cave_queries = 0;
    std::uint64_t cave_noise_calls = 0;
    std::uint64_t biome_queries = 0;
    std::uint64_t biome_cache_hits = 0;
    std::uint64_t biome_cache_misses = 0;
    std::uint64_t base_block_queries = 0;
    std::uint64_t fluid_queries = 0;
    std::uint64_t surface_block_queries = 0;
    std::uint64_t filler_block_queries = 0;
    std::uint64_t ore_queries = 0;
    std::uint64_t ore_rule_checks = 0;
    std::uint64_t ore_patch_cache_hits = 0;
    std::uint64_t ore_patch_cache_misses = 0;
    std::uint64_t ore_patch_candidates = 0;
    std::uint64_t buried_ore_queries = 0;
    std::uint64_t buried_neighbor_queries = 0;
    std::uint64_t tree_candidate_rolls = 0;
    std::uint64_t tree_base_queries = 0;
    std::uint64_t tree_base_cache_hits = 0;
    std::uint64_t tree_base_cache_misses = 0;
    std::uint64_t tree_surface_queries = 0;
    std::uint64_t tree_clearance_queries = 0;
    std::uint64_t accepted_tree_bases = 0;
    std::uint64_t tree_trunk_voxel_tests = 0;
    std::uint64_t tree_canopy_voxel_tests = 0;
};

struct ChunkMeshCounters {
    std::uint64_t chunk_lookups = 0;
    std::uint64_t visible_block_samples = 0;
    std::uint64_t renderable_blocks = 0;
    std::uint64_t neighbor_samples = 0;
    std::uint64_t external_neighbor_samples = 0;
    std::uint64_t missing_neighbor_samples = 0;
    std::uint64_t opaque_faces = 0;
    std::uint64_t transparent_faces = 0;
};

struct ChunkGenerationProfile {
    ChunkCoord coord {};
    WorldGenerationType generation_type = WorldGenerationType::normal;
    std::uint64_t total_nanoseconds = 0;
    std::array<std::uint64_t, static_cast<std::size_t>(ChunkGenerationStage::count)> stage_nanoseconds {};
    ChunkGenerationCounters counters {};
};

struct ChunkMeshProfile {
    ChunkCoord coord {};
    SliceView slice = SliceView::xyz;
    int hidden_block = 0;
    std::uint64_t total_nanoseconds = 0;
    std::array<std::uint64_t, static_cast<std::size_t>(ChunkMeshStage::count)> stage_nanoseconds {};
    ChunkMeshCounters counters {};
};

const char* chunk_generation_stage_label(ChunkGenerationStage stage);
const char* chunk_mesh_stage_label(ChunkMeshStage stage);

class ChunkPipelineProfiler {
public:
    static constexpr bool compiled()
    {
#if defined(HYPERCRAFT_ENABLE_CHUNK_PROFILING)
        return true;
#else
        return false;
#endif
    }

    static void reset();
    static std::vector<ChunkGenerationProfile> generation_profiles();
    static std::vector<ChunkMeshProfile> mesh_profiles();
};

#if defined(HYPERCRAFT_ENABLE_CHUNK_PROFILING)
namespace detail {

inline thread_local ChunkGenerationProfile* active_generation_profile = nullptr;
inline thread_local ChunkMeshProfile* active_mesh_profile = nullptr;

inline ChunkGenerationProfile* active_chunk_generation_profile() { return active_generation_profile; }

inline ChunkMeshProfile* active_chunk_mesh_profile() { return active_mesh_profile; }

class ChunkGenerationProfileScope {
public:
    ChunkGenerationProfileScope(ChunkCoord coord, WorldGenerationType generation_type);
    ~ChunkGenerationProfileScope();

    ChunkGenerationProfileScope(const ChunkGenerationProfileScope&) = delete;
    ChunkGenerationProfileScope& operator=(const ChunkGenerationProfileScope&) = delete;
    void begin_stage(ChunkGenerationStage stage);

private:
    void finish_stage();

    ChunkGenerationProfile profile_ {};
    ChunkGenerationProfile* previous_ = nullptr;
    std::chrono::steady_clock::time_point started_ {};
    std::chrono::steady_clock::time_point stage_started_ {};
    ChunkGenerationStage active_stage_ = ChunkGenerationStage::count;
};

class ChunkMeshProfileScope {
public:
    ChunkMeshProfileScope(ChunkCoord coord, SliceView slice, int hidden_block);
    ~ChunkMeshProfileScope();

    ChunkMeshProfileScope(const ChunkMeshProfileScope&) = delete;
    ChunkMeshProfileScope& operator=(const ChunkMeshProfileScope&) = delete;
    void begin_stage(ChunkMeshStage stage);

private:
    void finish_stage();

    ChunkMeshProfile profile_ {};
    ChunkMeshProfile* previous_ = nullptr;
    std::chrono::steady_clock::time_point started_ {};
    std::chrono::steady_clock::time_point stage_started_ {};
    ChunkMeshStage active_stage_ = ChunkMeshStage::count;
};

} // namespace detail
#endif

} // namespace hypercraft

#if defined(HYPERCRAFT_ENABLE_CHUNK_PROFILING)
#define HYPERCRAFT_PROFILE_GENERATION_SCOPE(coord, generation_type)                                                    \
    ::hypercraft::detail::ChunkGenerationProfileScope hypercraft_generation_profile_scope((coord), (generation_type))
#define HYPERCRAFT_PROFILE_GENERATION_STAGE(stage) hypercraft_generation_profile_scope.begin_stage((stage))
#define HYPERCRAFT_PROFILE_GENERATION_COUNT(field)                                                                     \
    do {                                                                                                               \
        if (auto* hypercraft_profile = ::hypercraft::detail::active_chunk_generation_profile()) {                      \
            ++hypercraft_profile->counters.field;                                                                      \
        }                                                                                                              \
    } while (false)
#define HYPERCRAFT_PROFILE_GENERATION_ADD(field, amount)                                                               \
    do {                                                                                                               \
        if (auto* hypercraft_profile = ::hypercraft::detail::active_chunk_generation_profile()) {                      \
            hypercraft_profile->counters.field += static_cast<std::uint64_t>(amount);                                  \
        }                                                                                                              \
    } while (false)
#define HYPERCRAFT_PROFILE_MESH_SCOPE(coord, slice, hidden_block)                                                      \
    ::hypercraft::detail::ChunkMeshProfileScope hypercraft_mesh_profile_scope((coord), (slice), (hidden_block))
#define HYPERCRAFT_PROFILE_MESH_STAGE(stage) hypercraft_mesh_profile_scope.begin_stage((stage))
#define HYPERCRAFT_PROFILE_MESH_COUNT(field)                                                                           \
    do {                                                                                                               \
        if (auto* hypercraft_profile = ::hypercraft::detail::active_chunk_mesh_profile()) {                            \
            ++hypercraft_profile->counters.field;                                                                      \
        }                                                                                                              \
    } while (false)
#else
#define HYPERCRAFT_PROFILE_GENERATION_SCOPE(coord, generation_type) ((void)0)
#define HYPERCRAFT_PROFILE_GENERATION_STAGE(stage) ((void)0)
#define HYPERCRAFT_PROFILE_GENERATION_COUNT(field) ((void)0)
#define HYPERCRAFT_PROFILE_GENERATION_ADD(field, amount) ((void)0)
#define HYPERCRAFT_PROFILE_MESH_SCOPE(coord, slice, hidden_block) ((void)0)
#define HYPERCRAFT_PROFILE_MESH_STAGE(stage) ((void)0)
#define HYPERCRAFT_PROFILE_MESH_COUNT(field) ((void)0)
#endif
