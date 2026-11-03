#include "hypercraft/world/terrain_generator.hpp"

#include "hypercraft/performance/chunk_pipeline_profiler.hpp"
#include "hypercraft/world/biome_registry.hpp"
#include "hypercraft/world/content_registry.hpp"
#include "hypercraft/world/ore_registry.hpp"
#include "hypercraft/world/tree_registry.hpp"
#include "hypercraft/world/world_seed.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace hypercraft {

TerrainChunkGeneration::TerrainChunkGeneration(Handle handle)
    : handle_(handle)
{
}

TerrainChunkGeneration::TerrainChunkGeneration(TerrainChunkGeneration&& other) noexcept
    : handle_(std::exchange(other.handle_, {}))
{
}

TerrainChunkGeneration& TerrainChunkGeneration::operator=(TerrainChunkGeneration&& other) noexcept
{
    if (this != &other) {
        if (handle_) {
            handle_.destroy();
        }
        handle_ = std::exchange(other.handle_, {});
    }
    return *this;
}

TerrainChunkGeneration::~TerrainChunkGeneration()
{
    if (handle_) {
        handle_.destroy();
    }
}

bool TerrainChunkGeneration::advance()
{
    if (handle_ && !handle_.done()) {
        handle_.resume();
    }
    return done();
}

bool TerrainChunkGeneration::done() const
{
    return !handle_ || handle_.done();
}

Chunk TerrainChunkGeneration::take_chunk()
{
    if (!handle_ || !handle_.done()) {
        throw std::logic_error("chunk generation is not complete");
    }
    promise_type& promise = handle_.promise();
    if (promise.error) {
        std::rethrow_exception(promise.error);
    }
    if (!promise.result.has_value()) {
        throw std::logic_error("completed chunk generation has no result");
    }
    return std::move(*promise.result);
}

TerrainChunkGeneration TerrainChunkGeneration::promise_type::get_return_object()
{
    return TerrainChunkGeneration {Handle::from_promise(*this)};
}

std::suspend_always TerrainChunkGeneration::promise_type::initial_suspend() const noexcept
{
    return {};
}

std::suspend_always TerrainChunkGeneration::promise_type::final_suspend() const noexcept
{
    return {};
}

std::suspend_always TerrainChunkGeneration::promise_type::yield_value(std::size_t) const noexcept
{
    return {};
}

void TerrainChunkGeneration::promise_type::return_value(Chunk chunk)
{
    result.emplace(std::move(chunk));
}

void TerrainChunkGeneration::promise_type::unhandled_exception() noexcept
{
    error = std::current_exception();
}

namespace {

constexpr int water_level = 0;
constexpr int max_scan_y = 96;
constexpr int min_scan_y = -64;
constexpr double cave_noise_scale = 0.026;
constexpr double cave_noise_height_scale = 64.0;
constexpr double cave_noise_brief_half_width = 3.0;
constexpr double climate_noise_scale = 0.004;
constexpr double climate_noise_contrast = 1.35;
constexpr int biome_tile_size = 8;

std::uint32_t hash_3d(int x, int y, int z, std::uint32_t seed);
std::uint32_t hash_4d(int x, int y, int z, int w, std::uint32_t seed);

struct ColumnKey {
    int x = 0;
    int z = 0;
    int w = 0;

    bool operator==(const ColumnKey& other) const
    {
        return x == other.x && z == other.z && w == other.w;
    }
};

struct ColumnKeyHash {
    std::size_t operator()(ColumnKey key) const
    {
        return hash_4d(key.x, 0, key.z, key.w, 0xC011A4u);
    }
};

struct BiomeTileKey {
    int x = 0;
    int z = 0;
    int w = 0;

    friend bool operator==(BiomeTileKey left, BiomeTileKey right) = default;
};

struct BiomeTileKeyHash {
    std::size_t operator()(BiomeTileKey key) const
    {
        return hash_4d(key.x, 0, key.z, key.w, 0xB10E7u);
    }
};

struct BiomeTile {
    static constexpr std::size_t volume = biome_tile_size * biome_tile_size * biome_tile_size;
    std::array<const BiomeDefinition*, volume> definitions {};

    static constexpr std::size_t index(int x, int z, int w)
    {
        return static_cast<std::size_t>(x + biome_tile_size * (z + biome_tile_size * w));
    }
};

struct TreeBase {
    BlockCoord base;
    BlockCoord top;
    const TreeDefinition* definition = nullptr;
    int height = 0;
    int leaf_radius_squared = 0;
    int canopy_extent = 0;
    int canopy_min_y = 0;
    int canopy_max_y = 0;
    std::vector<BlockCoord> trunk;
};

struct TreeCandidate {
    BlockCoord base;
    std::uint32_t roll = 0;
};

struct TreeCandidateTileKey {
    std::uint32_t seed = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    int w = 0;
    std::uint32_t max_chance = 0;

    friend bool operator==(TreeCandidateTileKey left, TreeCandidateTileKey right) = default;
};

struct TreeCandidateTileKeyHash {
    std::size_t operator()(TreeCandidateTileKey key) const
    {
        return hash_4d(key.x, key.y, key.z, key.w, key.seed ^ key.max_chance ^ 0x7A33u);
    }
};

struct TreeCandidateTile {
    std::vector<TreeCandidate> candidates;
};

template <typename Key, typename Value, typename Hash>
class BoundedConcurrentCache {
public:
    struct Stats {
        std::size_t bytes = 0;
        std::size_t entries = 0;
        std::uint64_t hits = 0;
        std::uint64_t misses = 0;
        std::uint64_t coalesced_waits = 0;
    };

    explicit BoundedConcurrentCache(std::size_t max_bytes)
        : max_bytes_(max_bytes)
    {
    }

    template <typename Builder, typename Weigher>
    std::shared_ptr<const Value> get_or_compute(
        const Key& key,
        Builder&& builder,
        Weigher&& weigher,
        bool* cache_hit = nullptr)
    {
        {
            std::unique_lock lock(mutex_);
            for (;;) {
                const auto found = values_.find(key);
                if (found != values_.end()) {
                    if (cache_hit != nullptr) {
                        *cache_hit = true;
                    }
                    ++hits_;
                    lru_.splice(lru_.begin(), lru_, found->second.lru);
                    return found->second.value;
                }
                if (in_flight_.find(key) == in_flight_.end()) {
                    in_flight_.emplace(key);
                    if (cache_hit != nullptr) {
                        *cache_hit = false;
                    }
                    ++misses_;
                    break;
                }
                ++coalesced_waits_;
                ready_.wait(lock, [&] { return in_flight_.find(key) == in_flight_.end(); });
            }
        }

        std::shared_ptr<const Value> value;
        try {
            value = std::make_shared<const Value>(std::forward<Builder>(builder)());
        } catch (...) {
            std::lock_guard lock(mutex_);
            in_flight_.erase(key);
            ready_.notify_all();
            throw;
        }

        constexpr std::size_t cache_entry_overhead =
            sizeof(Key) + sizeof(std::shared_ptr<const Value>) + sizeof(std::size_t) + sizeof(void*) * 6U;
        const std::size_t value_bytes =
            cache_entry_overhead + std::max(sizeof(Value), static_cast<std::size_t>(weigher(*value)));
        {
            std::lock_guard lock(mutex_);
            while (!lru_.empty() && bytes_ + value_bytes > max_bytes_) {
                const Key& victim_key = lru_.back();
                const auto victim = values_.find(victim_key);
                bytes_ -= victim->second.bytes;
                values_.erase(victim);
                lru_.pop_back();
            }
            if (value_bytes <= max_bytes_) {
                lru_.push_front(key);
                values_.emplace(key, Entry {value, value_bytes, lru_.begin()});
                bytes_ += value_bytes;
            }
            in_flight_.erase(key);
            ready_.notify_all();
        }
        return value;
    }

    Stats stats() const
    {
        std::lock_guard lock(mutex_);
        return {bytes_, values_.size(), hits_, misses_, coalesced_waits_};
    }

private:
    struct Entry {
        std::shared_ptr<const Value> value;
        std::size_t bytes = 0;
        typename std::list<Key>::iterator lru;
    };

    std::size_t max_bytes_ = 0;
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::unordered_map<Key, Entry, Hash> values_;
    std::unordered_set<Key, Hash> in_flight_;
    std::list<Key> lru_;
    std::size_t bytes_ = 0;
    std::uint64_t hits_ = 0;
    std::uint64_t misses_ = 0;
    std::uint64_t coalesced_waits_ = 0;
};

struct ChunkGenerationScratch {
    static constexpr int classification_padding = 4;
    static constexpr int cached_y_size = chunk_size + classification_padding + 1;
    static constexpr int cached_solid_count = chunk_size * cached_y_size * chunk_size * chunk_size;
    static constexpr int cached_column_count = chunk_size * chunk_size * chunk_size;
    static constexpr int neighbor_axis_size = chunk_size + 2;
    static constexpr int neighbor_y_size = cached_y_size + 1;
    static constexpr int neighbor_solid_count =
        neighbor_axis_size * neighbor_y_size * neighbor_axis_size * neighbor_axis_size;
    static constexpr int neighbor_column_count = neighbor_axis_size * neighbor_axis_size * neighbor_axis_size;
    static constexpr std::int8_t unknown_solid = -1;

    ChunkGenerationScratch()
    {
        raw_solid.fill(unknown_solid);
    }

    static constexpr std::size_t solid_index(int x, int y, int z, int w)
    {
        return static_cast<std::size_t>(x + chunk_size * (z + chunk_size * (y + cached_y_size * w)));
    }

    static constexpr std::size_t column_index(int x, int z, int w)
    {
        return static_cast<std::size_t>(x + chunk_size * (z + chunk_size * w));
    }

    static constexpr std::size_t neighbor_solid_index(int x, int y, int z, int w)
    {
        return static_cast<std::size_t>(
            (x + 1) + neighbor_axis_size * ((z + 1) + neighbor_axis_size * ((y + 1) + neighbor_y_size * (w + 1))));
    }

    static constexpr std::size_t neighbor_column_index(int x, int z, int w)
    {
        return static_cast<std::size_t>((x + 1) + neighbor_axis_size * ((z + 1) + neighbor_axis_size * (w + 1)));
    }

    bool raw_solid_is_known(int x, int y, int z, int w) const
    {
        return raw_solid[neighbor_solid_index(x, y, z, w)] != unknown_solid;
    }

    bool raw_solid_at(int x, int y, int z, int w) const
    {
        return raw_solid[neighbor_solid_index(x, y, z, w)] == 1;
    }

    void set_raw_solid(int x, int y, int z, int w, bool value)
    {
        raw_solid[neighbor_solid_index(x, y, z, w)] = value ? 1 : 0;
    }

    std::array<std::int8_t, neighbor_solid_count> raw_solid {};
    std::array<bool, cached_solid_count> solid {};
    std::array<const BiomeDefinition*, cached_column_count> biomes {};
    std::array<double, neighbor_column_count> height_guides {};
    std::array<bool, neighbor_column_count> height_guide_known {};
};

constexpr std::array<BlockCoord, 8> raw_solid_neighbor_offsets {{
    {1, 0, 0, 0},
    {-1, 0, 0, 0},
    {0, 1, 0, 0},
    {0, -1, 0, 0},
    {0, 0, 1, 0},
    {0, 0, -1, 0},
    {0, 0, 0, 1},
    {0, 0, 0, -1},
}};

template <typename SolidSampler>
bool has_only_raw_solid_neighbors(BlockCoord coord, SolidSampler&& sample_solid)
{
    HYPERCRAFT_PROFILE_GENERATION_COUNT(buried_ore_queries);
    for (const BlockCoord offset : raw_solid_neighbor_offsets) {
        HYPERCRAFT_PROFILE_GENERATION_COUNT(buried_neighbor_queries);
        if (!sample_solid({coord.x + offset.x, coord.y + offset.y, coord.z + offset.z, coord.w + offset.w})) {
            return false;
        }
    }
    return true;
}

int spherical_leaf_extent(int radius_squared)
{
    int extent = 0;
    while (extent * extent < radius_squared) {
        ++extent;
    }
    return extent;
}

int max_leaf_extent(const TreeDefinition& tree)
{
    return tree.leaf_shape == LeafShape::conal
        ? tree.max_leaf_radius
        : spherical_leaf_extent(tree.max_leaf_radius);
}

int max_tree_horizontal_extent(const TreeDefinition& tree)
{
    const int trunk_drift = tree.trunk_shape == TrunkShape::twisted ? tree.max_trunk_height : 0;
    return trunk_drift + max_leaf_extent(tree);
}

struct TreeOverlapBounds {
    int horizontal_extent = 0;
    int min_base_y = 0;
    int max_base_y = 0;
};

TreeOverlapBounds tree_overlap_bounds(const TreeDefinition& tree, int min_chunk_y, int max_chunk_y)
{
    const int leaf_extent = max_leaf_extent(tree);
    const int leaf_up_extent = tree.leaf_shape == LeafShape::conal ? 1 : leaf_extent;
    const int leaf_down_extent = tree.leaf_shape == LeafShape::conal ? tree.max_leaf_radius : leaf_extent;
    return {
        max_tree_horizontal_extent(tree),
        min_chunk_y - tree.max_trunk_height - leaf_up_extent,
        max_chunk_y - std::min(1, tree.min_trunk_height - leaf_down_extent),
    };
}

struct OrePatchKey {
    std::uint32_t seed = 0;
    int salt = 0;
    int spacing = 0;
    std::uint32_t chance = 0;
    std::uint32_t chance_denominator = 0;
    int patch_radius = 0;
    int min_count = 0;
    int max_count = 0;
    int cell_x = 0;
    int cell_y = 0;
    int cell_z = 0;
    int cell_w = 0;

    bool operator==(const OrePatchKey& other) const
    {
        return seed == other.seed &&
            salt == other.salt &&
            spacing == other.spacing &&
            chance == other.chance &&
            chance_denominator == other.chance_denominator &&
            patch_radius == other.patch_radius &&
            min_count == other.min_count &&
            max_count == other.max_count &&
            cell_x == other.cell_x &&
            cell_y == other.cell_y &&
            cell_z == other.cell_z &&
            cell_w == other.cell_w;
    }
};

struct OrePatchKeyHash {
    std::size_t operator()(OrePatchKey key) const
    {
        const int parameter_mix = key.spacing ^
            (key.patch_radius << 4) ^
            (key.min_count << 8) ^
            (key.max_count << 16);
        return hash_4d(
            key.cell_x + key.salt,
            key.cell_y ^ parameter_mix,
            key.cell_z,
            key.cell_w,
            key.seed ^ key.chance ^ (key.chance_denominator << 8U));
    }
};

double fade(double t)
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

double lerp(double a, double b, double t)
{
    return a + (b - a) * t;
}

double climate_value_from_noise(double noise)
{
    return std::clamp(0.5 + noise * climate_noise_contrast, 0.0, 1.0);
}

std::uint32_t hash_3d(int x, int y, int z, std::uint32_t seed)
{
    std::uint32_t value = static_cast<std::uint32_t>(x) * 374761393u
        ^ static_cast<std::uint32_t>(y) * 668265263u
        ^ static_cast<std::uint32_t>(z) * 2246822519u
        ^ seed * 2246822519u;
    value ^= value >> 13u;
    value *= 3266489917u;
    value ^= value >> 16u;
    return value;
}

std::uint32_t hash_4d(int x, int y, int z, int w, std::uint32_t seed)
{
    std::uint32_t value = static_cast<std::uint32_t>(x) * 374761393u
        ^ static_cast<std::uint32_t>(y) * 668265263u
        ^ static_cast<std::uint32_t>(z) * 2246822519u
        ^ static_cast<std::uint32_t>(w) * 3266489917u
        ^ seed * 2246822519u;
    value ^= value >> 13u;
    value *= 3266489917u;
    value ^= value >> 16u;
    return value;
}

std::uint32_t mix_lattice_seed(std::uint32_t hash, std::uint32_t seed)
{
    hash += seed * 2654435761u;
    hash = (hash ^ (hash >> 13u)) * 1274126177u;
    return hash ^ (hash >> 16u);
}

std::uint32_t hash_lattice_4d(int x, int y, int z, int w, std::uint32_t seed)
{
    std::uint32_t hash = static_cast<std::uint32_t>(x) * 374761393u;
    hash += static_cast<std::uint32_t>(y) * 2246822519u;
    hash += static_cast<std::uint32_t>(z) * 668265263u;
    hash += static_cast<std::uint32_t>(w) * 3266489917u;
    return mix_lattice_seed(hash, seed);
}

double gradient_dot_from_index_3d(std::uint8_t index, double dx, double dy, double dz)
{
    constexpr double diagonal = 0.5773502691896258;
    constexpr double gradients[12][3] = {
        {diagonal, diagonal, diagonal},
        {-diagonal, diagonal, diagonal},
        {diagonal, -diagonal, diagonal},
        {diagonal, diagonal, -diagonal},
        {-diagonal, -diagonal, diagonal},
        {-diagonal, diagonal, -diagonal},
        {diagonal, -diagonal, -diagonal},
        {-diagonal, -diagonal, -diagonal},
        {1.0, 0.0, 0.0},
        {-1.0, 0.0, 0.0},
        {0.0, 1.0, 0.0},
        {0.0, -1.0, 0.0},
    };
    const auto& gradient = gradients[index];
    return gradient[0] * dx + gradient[1] * dy + gradient[2] * dz;
}

double gradient_dot(int x, int y, int z, double dx, double dy, double dz, std::uint32_t seed)
{
    return gradient_dot_from_index_3d(
        static_cast<std::uint8_t>(hash_3d(x, y, z, seed) % 12u),
        dx,
        dy,
        dz);
}

double gradient_dot_from_index_4d(
    std::uint8_t index,
    double dx,
    double dy,
    double dz,
    double dw)
{
    constexpr double diagonal = 0.5;
    constexpr double gradients[32][4] = {
        {diagonal, diagonal, diagonal, diagonal},
        {-diagonal, diagonal, diagonal, diagonal},
        {diagonal, -diagonal, diagonal, diagonal},
        {diagonal, diagonal, -diagonal, diagonal},
        {diagonal, diagonal, diagonal, -diagonal},
        {-diagonal, -diagonal, diagonal, diagonal},
        {-diagonal, diagonal, -diagonal, diagonal},
        {-diagonal, diagonal, diagonal, -diagonal},
        {diagonal, -diagonal, -diagonal, diagonal},
        {diagonal, -diagonal, diagonal, -diagonal},
        {diagonal, diagonal, -diagonal, -diagonal},
        {-diagonal, -diagonal, -diagonal, diagonal},
        {-diagonal, -diagonal, diagonal, -diagonal},
        {-diagonal, diagonal, -diagonal, -diagonal},
        {diagonal, -diagonal, -diagonal, -diagonal},
        {-diagonal, -diagonal, -diagonal, -diagonal},
        {1.0, 0.0, 0.0, 0.0},
        {-1.0, 0.0, 0.0, 0.0},
        {0.0, 1.0, 0.0, 0.0},
        {0.0, -1.0, 0.0, 0.0},
        {0.0, 0.0, 1.0, 0.0},
        {0.0, 0.0, -1.0, 0.0},
        {0.0, 0.0, 0.0, 1.0},
        {0.0, 0.0, 0.0, -1.0},
        {0.70710678118, 0.70710678118, 0.0, 0.0},
        {0.70710678118, 0.0, 0.70710678118, 0.0},
        {0.70710678118, 0.0, 0.0, 0.70710678118},
        {0.0, 0.70710678118, 0.70710678118, 0.0},
        {0.0, 0.70710678118, 0.0, 0.70710678118},
        {0.0, 0.0, 0.70710678118, 0.70710678118},
        {-0.70710678118, 0.70710678118, 0.0, 0.0},
        {0.0, -0.70710678118, 0.0, 0.70710678118},
    };
    const auto& gradient = gradients[index];
    return gradient[0] * dx + gradient[1] * dy + gradient[2] * dz + gradient[3] * dw;
}

double gradient_dot_4d(int x, int y, int z, int w, double dx, double dy, double dz, double dw, std::uint32_t seed)
{
    return gradient_dot_from_index_4d(
        static_cast<std::uint8_t>(hash_4d(x, y, z, w, seed) % 32u),
        dx,
        dy,
        dz,
        dw);
}

struct NoiseAxisSample {
    int lattice = 0;
    double offset = 0.0;
    double faded = 0.0;
};

struct RegularNoiseOctave3D {
    std::array<std::vector<NoiseAxisSample>, 3> axes;
    std::array<int, 3> lattice_min {};
    std::array<int, 3> lattice_size {};
    std::vector<std::uint8_t> gradients;

    std::size_t gradient_index(int x, int y, int z) const
    {
        return static_cast<std::size_t>(x - lattice_min[0]) +
            static_cast<std::size_t>(lattice_size[0]) *
                (static_cast<std::size_t>(y - lattice_min[1]) +
                    static_cast<std::size_t>(lattice_size[1]) *
                        static_cast<std::size_t>(z - lattice_min[2]));
    }
};

class RegularFractalNoise3D {
public:
    RegularFractalNoise3D(
        std::uint32_t seed,
        const std::array<std::vector<double>, 3>& coordinates)
    {
        double frequency = 1.0;
        for (std::size_t octave_index = 0; octave_index < octaves_.size(); ++octave_index) {
            RegularNoiseOctave3D& octave = octaves_[octave_index];
            for (std::size_t axis = 0; axis < octave.axes.size(); ++axis) {
                std::vector<NoiseAxisSample>& samples = octave.axes[axis];
                samples.reserve(coordinates[axis].size());
                int minimum = 0;
                int maximum = 0;
                bool first = true;
                for (const double coordinate : coordinates[axis]) {
                    const double scaled = coordinate * frequency;
                    const int lattice = static_cast<int>(std::floor(scaled));
                    samples.push_back({lattice, scaled - static_cast<double>(lattice), 0.0});
                    samples.back().faded = fade(samples.back().offset);
                    if (first) {
                        minimum = lattice;
                        maximum = lattice + 1;
                        first = false;
                    } else {
                        minimum = std::min(minimum, lattice);
                        maximum = std::max(maximum, lattice + 1);
                    }
                }
                octave.lattice_min[axis] = minimum;
                octave.lattice_size[axis] = maximum - minimum + 1;
            }

            const std::size_t gradient_count =
                static_cast<std::size_t>(octave.lattice_size[0]) *
                static_cast<std::size_t>(octave.lattice_size[1]) *
                static_cast<std::size_t>(octave.lattice_size[2]);
            octave.gradients.resize(gradient_count);
            const std::uint32_t salted_seed = seed ^ (static_cast<std::uint32_t>(octave_index) * 374761393u);
            for (int z = octave.lattice_min[2]; z < octave.lattice_min[2] + octave.lattice_size[2]; ++z) {
                for (int y = octave.lattice_min[1]; y < octave.lattice_min[1] + octave.lattice_size[1]; ++y) {
                    for (int x = octave.lattice_min[0]; x < octave.lattice_min[0] + octave.lattice_size[0]; ++x) {
                        octave.gradients[octave.gradient_index(x, y, z)] =
                            static_cast<std::uint8_t>(hash_3d(x, y, z, salted_seed) % 12u);
                    }
                }
            }
            frequency *= 2.0;
        }
    }

    double sample(std::size_t x, std::size_t y, std::size_t z) const
    {
        HYPERCRAFT_PROFILE_GENERATION_COUNT(fractal_3d_calls);
        double amplitude = 1.0;
        double total = 0.0;
        double normalization = 0.0;
        for (const RegularNoiseOctave3D& octave : octaves_) {
            HYPERCRAFT_PROFILE_GENERATION_COUNT(perlin_3d_calls);
            total += sample_octave(octave, x, y, z) * amplitude;
            normalization += amplitude;
            amplitude *= 0.5;
        }
        return total / normalization;
    }

private:
    static double gradient_dot(
        const RegularNoiseOctave3D& octave,
        int x,
        int y,
        int z,
        double dx,
        double dy,
        double dz)
    {
        return gradient_dot_from_index_3d(
            octave.gradients[octave.gradient_index(x, y, z)],
            dx,
            dy,
            dz);
    }

    static double sample_octave(
        const RegularNoiseOctave3D& octave,
        std::size_t x,
        std::size_t y,
        std::size_t z)
    {
        const NoiseAxisSample& sx = octave.axes[0][x];
        const NoiseAxisSample& sy = octave.axes[1][y];
        const NoiseAxisSample& sz = octave.axes[2][z];
        const int x1 = sx.lattice + 1;
        const int y1 = sy.lattice + 1;
        const int z1 = sz.lattice + 1;
        const double x00 = lerp(
            gradient_dot(octave, sx.lattice, sy.lattice, sz.lattice, sx.offset, sy.offset, sz.offset),
            gradient_dot(octave, x1, sy.lattice, sz.lattice, sx.offset - 1.0, sy.offset, sz.offset),
            sx.faded);
        const double x10 = lerp(
            gradient_dot(octave, sx.lattice, y1, sz.lattice, sx.offset, sy.offset - 1.0, sz.offset),
            gradient_dot(octave, x1, y1, sz.lattice, sx.offset - 1.0, sy.offset - 1.0, sz.offset),
            sx.faded);
        const double x01 = lerp(
            gradient_dot(octave, sx.lattice, sy.lattice, z1, sx.offset, sy.offset, sz.offset - 1.0),
            gradient_dot(octave, x1, sy.lattice, z1, sx.offset - 1.0, sy.offset, sz.offset - 1.0),
            sx.faded);
        const double x11 = lerp(
            gradient_dot(octave, sx.lattice, y1, z1, sx.offset, sy.offset - 1.0, sz.offset - 1.0),
            gradient_dot(octave, x1, y1, z1, sx.offset - 1.0, sy.offset - 1.0, sz.offset - 1.0),
            sx.faded);
        return lerp(lerp(x00, x10, sy.faded), lerp(x01, x11, sy.faded), sz.faded);
    }

    std::array<RegularNoiseOctave3D, 4> octaves_;
};

struct RegularNoiseOctave4D {
    std::array<std::vector<NoiseAxisSample>, 4> axes;
    std::array<int, 4> lattice_min {};
    std::array<int, 4> lattice_size {};
    std::vector<std::uint8_t> gradients;

    std::size_t gradient_index(int x, int y, int z, int w) const
    {
        return static_cast<std::size_t>(x - lattice_min[0]) +
            static_cast<std::size_t>(lattice_size[0]) *
                (static_cast<std::size_t>(y - lattice_min[1]) +
                    static_cast<std::size_t>(lattice_size[1]) *
                        (static_cast<std::size_t>(z - lattice_min[2]) +
                            static_cast<std::size_t>(lattice_size[2]) *
                                static_cast<std::size_t>(w - lattice_min[3])));
    }
};

class RegularFractalNoise4D {
public:
    RegularFractalNoise4D(
        std::uint32_t seed,
        const std::array<std::vector<double>, 4>& coordinates)
    {
        double frequency = 1.0;
        for (std::size_t octave_index = 0; octave_index < octaves_.size(); ++octave_index) {
            RegularNoiseOctave4D& octave = octaves_[octave_index];
            for (std::size_t axis = 0; axis < octave.axes.size(); ++axis) {
                std::vector<NoiseAxisSample>& samples = octave.axes[axis];
                samples.reserve(coordinates[axis].size());
                int minimum = 0;
                int maximum = 0;
                bool first = true;
                for (const double coordinate : coordinates[axis]) {
                    const double scaled = coordinate * frequency;
                    const int lattice = static_cast<int>(std::floor(scaled));
                    samples.push_back({lattice, scaled - static_cast<double>(lattice), 0.0});
                    samples.back().faded = fade(samples.back().offset);
                    if (first) {
                        minimum = lattice;
                        maximum = lattice + 1;
                        first = false;
                    } else {
                        minimum = std::min(minimum, lattice);
                        maximum = std::max(maximum, lattice + 1);
                    }
                }
                octave.lattice_min[axis] = minimum;
                octave.lattice_size[axis] = maximum - minimum + 1;
            }

            const std::size_t gradient_count =
                static_cast<std::size_t>(octave.lattice_size[0]) *
                static_cast<std::size_t>(octave.lattice_size[1]) *
                static_cast<std::size_t>(octave.lattice_size[2]) *
                static_cast<std::size_t>(octave.lattice_size[3]);
            octave.gradients.resize(gradient_count);
            const std::uint32_t salted_seed = seed ^ (static_cast<std::uint32_t>(octave_index) * 374761393u);
            for (int w = octave.lattice_min[3]; w < octave.lattice_min[3] + octave.lattice_size[3]; ++w) {
                for (int z = octave.lattice_min[2]; z < octave.lattice_min[2] + octave.lattice_size[2]; ++z) {
                    for (int y = octave.lattice_min[1]; y < octave.lattice_min[1] + octave.lattice_size[1]; ++y) {
                        for (int x = octave.lattice_min[0]; x < octave.lattice_min[0] + octave.lattice_size[0]; ++x) {
                            octave.gradients[octave.gradient_index(x, y, z, w)] =
                                static_cast<std::uint8_t>(hash_4d(x, y, z, w, salted_seed) % 32u);
                        }
                    }
                }
            }
            frequency *= 2.0;
        }
    }

    double sample(std::size_t x, std::size_t y, std::size_t z, std::size_t w) const
    {
        HYPERCRAFT_PROFILE_GENERATION_COUNT(fractal_4d_calls);
        double amplitude = 1.0;
        double total = 0.0;
        double normalization = 0.0;
        for (const RegularNoiseOctave4D& octave : octaves_) {
            HYPERCRAFT_PROFILE_GENERATION_COUNT(perlin_4d_calls);
            total += sample_octave(octave, x, y, z, w) * amplitude;
            normalization += amplitude;
            amplitude *= 0.5;
        }
        return total / normalization;
    }

private:
    static double gradient_dot(
        const RegularNoiseOctave4D& octave,
        int x,
        int y,
        int z,
        int w,
        double dx,
        double dy,
        double dz,
        double dw)
    {
        return gradient_dot_from_index_4d(
            octave.gradients[octave.gradient_index(x, y, z, w)],
            dx,
            dy,
            dz,
            dw);
    }

    static double sample_octave(
        const RegularNoiseOctave4D& octave,
        std::size_t x,
        std::size_t y,
        std::size_t z,
        std::size_t w)
    {
        const NoiseAxisSample& sx = octave.axes[0][x];
        const NoiseAxisSample& sy = octave.axes[1][y];
        const NoiseAxisSample& sz = octave.axes[2][z];
        const NoiseAxisSample& sw = octave.axes[3][w];
        const int x1 = sx.lattice + 1;
        const int y1 = sy.lattice + 1;
        const int z1 = sz.lattice + 1;
        const int w1 = sw.lattice + 1;
        const auto corner = [&](int xi, int yi, int zi, int wi, double ox, double oy, double oz, double ow) {
            return gradient_dot(octave, xi, yi, zi, wi, ox, oy, oz, ow);
        };

        const double x000 = lerp(corner(sx.lattice, sy.lattice, sz.lattice, sw.lattice, sx.offset, sy.offset, sz.offset, sw.offset), corner(x1, sy.lattice, sz.lattice, sw.lattice, sx.offset - 1.0, sy.offset, sz.offset, sw.offset), sx.faded);
        const double x100 = lerp(corner(sx.lattice, y1, sz.lattice, sw.lattice, sx.offset, sy.offset - 1.0, sz.offset, sw.offset), corner(x1, y1, sz.lattice, sw.lattice, sx.offset - 1.0, sy.offset - 1.0, sz.offset, sw.offset), sx.faded);
        const double x010 = lerp(corner(sx.lattice, sy.lattice, z1, sw.lattice, sx.offset, sy.offset, sz.offset - 1.0, sw.offset), corner(x1, sy.lattice, z1, sw.lattice, sx.offset - 1.0, sy.offset, sz.offset - 1.0, sw.offset), sx.faded);
        const double x110 = lerp(corner(sx.lattice, y1, z1, sw.lattice, sx.offset, sy.offset - 1.0, sz.offset - 1.0, sw.offset), corner(x1, y1, z1, sw.lattice, sx.offset - 1.0, sy.offset - 1.0, sz.offset - 1.0, sw.offset), sx.faded);
        const double x001 = lerp(corner(sx.lattice, sy.lattice, sz.lattice, w1, sx.offset, sy.offset, sz.offset, sw.offset - 1.0), corner(x1, sy.lattice, sz.lattice, w1, sx.offset - 1.0, sy.offset, sz.offset, sw.offset - 1.0), sx.faded);
        const double x101 = lerp(corner(sx.lattice, y1, sz.lattice, w1, sx.offset, sy.offset - 1.0, sz.offset, sw.offset - 1.0), corner(x1, y1, sz.lattice, w1, sx.offset - 1.0, sy.offset - 1.0, sz.offset, sw.offset - 1.0), sx.faded);
        const double x011 = lerp(corner(sx.lattice, sy.lattice, z1, w1, sx.offset, sy.offset, sz.offset - 1.0, sw.offset - 1.0), corner(x1, sy.lattice, z1, w1, sx.offset - 1.0, sy.offset, sz.offset - 1.0, sw.offset - 1.0), sx.faded);
        const double x111 = lerp(corner(sx.lattice, y1, z1, w1, sx.offset, sy.offset - 1.0, sz.offset - 1.0, sw.offset - 1.0), corner(x1, y1, z1, w1, sx.offset - 1.0, sy.offset - 1.0, sz.offset - 1.0, sw.offset - 1.0), sx.faded);

        const double y00 = lerp(x000, x100, sy.faded);
        const double y10 = lerp(x010, x110, sy.faded);
        const double y01 = lerp(x001, x101, sy.faded);
        const double y11 = lerp(x011, x111, sy.faded);
        return lerp(lerp(y00, y10, sz.faded), lerp(y01, y11, sz.faded), sw.faded);
    }

    std::array<RegularNoiseOctave4D, 4> octaves_;
};

std::array<std::vector<double>, 4> regular_noise_coordinates(
    BlockCoord minimum,
    std::array<int, 4> counts,
    std::array<double, 4> scales,
    std::array<double, 4> offsets)
{
    std::array<std::vector<double>, 4> result;
    const std::array<int, 4> starts {minimum.x, minimum.y, minimum.z, minimum.w};
    for (std::size_t axis = 0; axis < result.size(); ++axis) {
        result[axis].reserve(static_cast<std::size_t>(counts[axis]));
        for (int index = 0; index < counts[axis]; ++index) {
            result[axis].push_back(
                static_cast<double>(starts[axis] + index) * scales[axis] + offsets[axis]);
        }
    }
    return result;
}

std::array<std::vector<double>, 3> height_noise_coordinates(
    int minimum_x,
    int minimum_z,
    int minimum_w,
    double scale,
    std::array<double, 3> offsets)
{
    std::array<std::vector<double>, 3> result;
    const std::array<int, 3> starts {minimum_x, minimum_z, minimum_w};
    for (std::size_t axis = 0; axis < result.size(); ++axis) {
        result[axis].reserve(chunk_size);
        for (int index = 0; index < chunk_size; ++index) {
            result[axis].push_back(
                static_cast<double>(starts[axis] + index) * scale / 3.0 + offsets[axis]);
        }
    }
    return result;
}

} // namespace

struct TerrainGenerator::CachedOrePatch {
    bool active = false;
    BlockCoord center {};
    std::vector<BlockCoord> offsets;
};

struct TerrainGenerator::CacheState {
    static constexpr std::size_t biome_cache_bytes = 16U * 1024U * 1024U;
    static constexpr std::size_t height_cache_bytes = 2U * 1024U * 1024U;
    static constexpr std::size_t ore_patch_cache_bytes = 12U * 1024U * 1024U;
    static constexpr std::size_t tree_candidate_cache_bytes = 64U * 1024U * 1024U;
    static constexpr std::size_t accepted_tree_cache_bytes = 8U * 1024U * 1024U;

    BoundedConcurrentCache<BiomeTileKey, BiomeTile, BiomeTileKeyHash> biomes {biome_cache_bytes};
    BoundedConcurrentCache<ColumnKey, int, ColumnKeyHash> heights {height_cache_bytes};
    BoundedConcurrentCache<OrePatchKey, CachedOrePatch, OrePatchKeyHash> ore_patches {ore_patch_cache_bytes};
    BoundedConcurrentCache<TreeCandidateTileKey, TreeCandidateTile, TreeCandidateTileKeyHash> tree_candidates {
        tree_candidate_cache_bytes};
    BoundedConcurrentCache<BlockCoord, TreeBase, BlockCoordHash> accepted_trees {accepted_tree_cache_bytes};

    std::shared_ptr<const BiomeTile> biome_tile(
        const TerrainGenerator& generator,
        BiomeTileKey key,
        bool* cache_hit = nullptr)
    {
        return biomes.get_or_compute(
            key,
            [&] {
                BiomeTile tile;
                const int min_x = key.x * biome_tile_size;
                const int min_z = key.z * biome_tile_size;
                const int min_w = key.w * biome_tile_size;
                for (int local_w = 0; local_w < biome_tile_size; ++local_w) {
                    for (int local_z = 0; local_z < biome_tile_size; ++local_z) {
                        for (int local_x = 0; local_x < biome_tile_size; ++local_x) {
                            const int x = min_x + local_x;
                            const int z = min_z + local_z;
                            const int w = min_w + local_w;
                            const double temperature = climate_value_from_noise(generator.fractal_noise_4d(
                                static_cast<double>(x) * climate_noise_scale + 900.0,
                                0.0,
                                static_cast<double>(z) * climate_noise_scale - 600.0,
                                static_cast<double>(w) * climate_noise_scale + 300.0));
                            const double precipitation = climate_value_from_noise(generator.fractal_noise_4d(
                                static_cast<double>(x) * climate_noise_scale - 350.0,
                                100.0,
                                static_cast<double>(z) * climate_noise_scale + 750.0,
                                static_cast<double>(w) * climate_noise_scale - 150.0));
                            tile.definitions[BiomeTile::index(local_x, local_z, local_w)] =
                                &generator.biome_registry_->definition_for_climate(temperature, precipitation);
                        }
                    }
                }
                return tile;
            },
            [](const BiomeTile&) { return sizeof(BiomeTile); },
            cache_hit);
    }
};

const char* world_generation_type_label(WorldGenerationType type)
{
    switch (type) {
    case WorldGenerationType::normal:
        return "NORMAL";
    case WorldGenerationType::flat:
        return "FLAT";
    case WorldGenerationType::high:
        return "HIGH";
    case WorldGenerationType::overhang:
        return "OVERHANG";
    }
    return "NORMAL";
}

double world_generation_diminishing_multiplier(WorldGenerationType type)
{
    switch (type) {
    case WorldGenerationType::normal:
        return 1.0;
    case WorldGenerationType::flat:
        return 1000.0;
    case WorldGenerationType::high:
        return 0.0;
    case WorldGenerationType::overhang:
        return -1.0;
    }
    return 1.0;
}

bool valid_world_generation_type(std::uint8_t value)
{
    return value < static_cast<std::uint8_t>(world_generation_type_count);
}

TerrainGenerator::TerrainGenerator()
    : TerrainGenerator(random_world_seed())
{
}

TerrainGenerator::TerrainGenerator(std::uint32_t seed)
    : TerrainGenerator(seed, WorldGenerationType::normal)
{
}

TerrainGenerator::TerrainGenerator(std::uint32_t seed, WorldGenerationType generation_type)
    : seed_(seed)
    , generation_type_(generation_type)
    , content_registry_(&ContentRegistry::default_registry())
    , biome_registry_(&BiomeRegistry::default_registry())
    , tree_registry_(&TreeRegistry::default_registry())
    , ore_registry_(&OreRegistry::default_registry())
    , cache_state_(std::make_shared<CacheState>())
{
}

TerrainGenerator::TerrainGenerator(
    std::uint32_t seed,
    WorldGenerationType generation_type,
    const ContentRegistry& content_registry,
    const BiomeRegistry& biome_registry,
    const TreeRegistry& tree_registry,
    const OreRegistry& ore_registry)
    : seed_(seed)
    , generation_type_(generation_type)
    , content_registry_(&content_registry)
    , biome_registry_(&biome_registry)
    , tree_registry_(&tree_registry)
    , ore_registry_(&ore_registry)
    , cache_state_(std::make_shared<CacheState>())
{
}

bool TerrainGenerator::content_is_solid(BlockId block) const
{
    return content_registry_->definition(block).solid;
}

TerrainGenerationCacheStats TerrainGenerator::cache_stats() const
{
    TerrainGenerationCacheStats result;
    const auto add = [&result](const auto& stats) {
        result.bytes += stats.bytes;
        result.entries += stats.entries;
        result.hits += stats.hits;
        result.misses += stats.misses;
        result.coalesced_waits += stats.coalesced_waits;
    };
    add(cache_state_->biomes.stats());
    add(cache_state_->heights.stats());
    add(cache_state_->ore_patches.stats());
    const auto candidate_stats = cache_state_->tree_candidates.stats();
    add(candidate_stats);
    add(cache_state_->accepted_trees.stats());
    result.tree_candidate_tile_bytes = candidate_stats.bytes;
    result.tree_candidate_tile_entries = candidate_stats.entries;
    result.tree_candidate_tile_hits = candidate_stats.hits;
    result.tree_candidate_tile_misses = candidate_stats.misses;
    return result;
}

double TerrainGenerator::perlin_noise(double x, double y, double z, std::uint32_t salt) const
{
    HYPERCRAFT_PROFILE_GENERATION_COUNT(perlin_3d_calls);
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int z0 = static_cast<int>(std::floor(z));
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;
    const int z1 = z0 + 1;
    const double dx = x - static_cast<double>(x0);
    const double dy = y - static_cast<double>(y0);
    const double dz = z - static_cast<double>(z0);
    const double tx = fade(dx);
    const double ty = fade(dy);
    const double tz = fade(dz);

    const std::uint32_t salted_seed = seed_ ^ (salt * 374761393u);
    const double x00 = lerp(
        gradient_dot(x0, y0, z0, dx, dy, dz, salted_seed),
        gradient_dot(x1, y0, z0, dx - 1.0, dy, dz, salted_seed),
        tx);
    const double x10 = lerp(
        gradient_dot(x0, y1, z0, dx, dy - 1.0, dz, salted_seed),
        gradient_dot(x1, y1, z0, dx - 1.0, dy - 1.0, dz, salted_seed),
        tx);
    const double x01 = lerp(
        gradient_dot(x0, y0, z1, dx, dy, dz - 1.0, salted_seed),
        gradient_dot(x1, y0, z1, dx - 1.0, dy, dz - 1.0, salted_seed),
        tx);
    const double x11 = lerp(
        gradient_dot(x0, y1, z1, dx, dy - 1.0, dz - 1.0, salted_seed),
        gradient_dot(x1, y1, z1, dx - 1.0, dy - 1.0, dz - 1.0, salted_seed),
        tx);

    return lerp(lerp(x00, x10, ty), lerp(x01, x11, ty), tz);
}

double TerrainGenerator::fractal_noise(double x, double y, double z) const
{
    HYPERCRAFT_PROFILE_GENERATION_COUNT(fractal_3d_calls);
    double amplitude = 1.0;
    double frequency = 1.0;
    double total = 0.0;
    double normalization = 0.0;
    for (int octave = 0; octave < 4; ++octave) {
        total += perlin_noise(x * frequency, y * frequency, z * frequency, static_cast<std::uint32_t>(octave)) * amplitude;
        normalization += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return total / normalization;
}

double TerrainGenerator::perlin_noise_4d(double x, double y, double z, double w, std::uint32_t salt) const
{
    HYPERCRAFT_PROFILE_GENERATION_COUNT(perlin_4d_calls);
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int z0 = static_cast<int>(std::floor(z));
    const int w0 = static_cast<int>(std::floor(w));
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;
    const int z1 = z0 + 1;
    const int w1 = w0 + 1;
    const double dx = x - static_cast<double>(x0);
    const double dy = y - static_cast<double>(y0);
    const double dz = z - static_cast<double>(z0);
    const double dw = w - static_cast<double>(w0);
    const double tx = fade(dx);
    const double ty = fade(dy);
    const double tz = fade(dz);
    const double tw = fade(dw);
    const std::uint32_t salted_seed = seed_ ^ (salt * 374761393u);

    const auto corner = [salted_seed](int xi, int yi, int zi, int wi, double ox, double oy, double oz, double ow) {
        return gradient_dot_4d(xi, yi, zi, wi, ox, oy, oz, ow, salted_seed);
    };

    const double x000 = lerp(corner(x0, y0, z0, w0, dx, dy, dz, dw), corner(x1, y0, z0, w0, dx - 1.0, dy, dz, dw), tx);
    const double x100 = lerp(corner(x0, y1, z0, w0, dx, dy - 1.0, dz, dw), corner(x1, y1, z0, w0, dx - 1.0, dy - 1.0, dz, dw), tx);
    const double x010 = lerp(corner(x0, y0, z1, w0, dx, dy, dz - 1.0, dw), corner(x1, y0, z1, w0, dx - 1.0, dy, dz - 1.0, dw), tx);
    const double x110 = lerp(corner(x0, y1, z1, w0, dx, dy - 1.0, dz - 1.0, dw), corner(x1, y1, z1, w0, dx - 1.0, dy - 1.0, dz - 1.0, dw), tx);
    const double x001 = lerp(corner(x0, y0, z0, w1, dx, dy, dz, dw - 1.0), corner(x1, y0, z0, w1, dx - 1.0, dy, dz, dw - 1.0), tx);
    const double x101 = lerp(corner(x0, y1, z0, w1, dx, dy - 1.0, dz, dw - 1.0), corner(x1, y1, z0, w1, dx - 1.0, dy - 1.0, dz, dw - 1.0), tx);
    const double x011 = lerp(corner(x0, y0, z1, w1, dx, dy, dz - 1.0, dw - 1.0), corner(x1, y0, z1, w1, dx - 1.0, dy, dz - 1.0, dw - 1.0), tx);
    const double x111 = lerp(corner(x0, y1, z1, w1, dx, dy - 1.0, dz - 1.0, dw - 1.0), corner(x1, y1, z1, w1, dx - 1.0, dy - 1.0, dz - 1.0, dw - 1.0), tx);

    const double y00 = lerp(x000, x100, ty);
    const double y10 = lerp(x010, x110, ty);
    const double y01 = lerp(x001, x101, ty);
    const double y11 = lerp(x011, x111, ty);
    return lerp(lerp(y00, y10, tz), lerp(y01, y11, tz), tw);
}

double TerrainGenerator::fractal_noise_4d(double x, double y, double z, double w) const
{
    HYPERCRAFT_PROFILE_GENERATION_COUNT(fractal_4d_calls);
    double amplitude = 1.0;
    double frequency = 1.0;
    double total = 0.0;
    double normalization = 0.0;
    for (int octave = 0; octave < 4; ++octave) {
        total += perlin_noise_4d(
                     x * frequency,
                     y * frequency,
                     z * frequency,
                     w * frequency,
                     static_cast<std::uint32_t>(octave))
            * amplitude;
        normalization += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return total / normalization;
}

double TerrainGenerator::height_guide_at(int x, int z, int w) const
{
    HYPERCRAFT_PROFILE_GENERATION_COUNT(height_guide_calls);
    constexpr double terrain_zoom = 3.0;
    const double world_x = static_cast<double>(x);
    const double world_z = static_cast<double>(z);
    const double world_w = static_cast<double>(w);
    const double broad_hills =
        fractal_noise(world_x * 0.035 / terrain_zoom, world_z * 0.035 / terrain_zoom, world_w * 0.035 / terrain_zoom) * 18.0;
    const double small_hills =
        fractal_noise(
            world_x * 0.11 / terrain_zoom + 100.0,
            world_z * 0.11 / terrain_zoom + 50.0,
            world_w * 0.11 / terrain_zoom - 100.0) *
        5.0;
    return std::round(18.0 + broad_hills + small_hills);
}

double TerrainGenerator::density_at(BlockCoord coord) const
{
    return density_at_with_height_guide(coord, height_guide_at(coord.x, coord.z, coord.w));
}

double TerrainGenerator::density_at_with_height_guide(BlockCoord coord, double base_height) const
{
    HYPERCRAFT_PROFILE_GENERATION_COUNT(density_calls);
    const double x = static_cast<double>(coord.x);
    const double y = static_cast<double>(coord.y);
    const double z = static_cast<double>(coord.z);
    const double w = static_cast<double>(coord.w);
    constexpr double density_zoom = 3.0;
    constexpr double normal_vertical_falloff = 0.045;
    constexpr double normal_height_falloff = 0.35 / 90.0;
    const double falloff_multiplier = world_generation_diminishing_multiplier(generation_type_);
    const double vertical_falloff = normal_vertical_falloff * falloff_multiplier;
    const double height_falloff = normal_height_falloff * falloff_multiplier;
    const double above_base = std::max(0.0, y - base_height);
    const double density_noise = fractal_noise_4d(
        x * 0.045 / density_zoom,
        y * 0.075 / density_zoom + 50.0,
        z * 0.045 / density_zoom,
        w * 0.045 / density_zoom - 50.0);
    return (base_height + 1.0 - y) * vertical_falloff + density_noise * 16.0 - above_base * height_falloff;
}

bool TerrainGenerator::is_generated_solid(BlockCoord coord) const
{
    return raw_generated_solid(coord) && !cave_carves_at(coord);
}

bool TerrainGenerator::raw_generated_solid(BlockCoord coord) const
{
    return raw_generated_solid_with_height_guide(coord, height_guide_at(coord.x, coord.z, coord.w));
}

bool TerrainGenerator::raw_generated_solid_with_height_guide(BlockCoord coord, double base_height) const
{
    HYPERCRAFT_PROFILE_GENERATION_COUNT(raw_solid_queries);
    return density_at_with_height_guide(coord, base_height) > 0.0;
}

int TerrainGenerator::height_at(int x, int z) const
{
    return height_at_4d(x, z, 0);
}

int TerrainGenerator::height_at(int x, int z, int w) const
{
    return height_at_4d(x, z, w);
}

Biome TerrainGenerator::biome_at(int x, int z, int w) const
{
    const BiomeDefinition& definition = biome_definition_at(x, z, w);
    return definition.builtin_biome.value_or(Biome::plains);
}

std::string_view TerrainGenerator::biome_id_at(int x, int z, int w) const
{
    return biome_definition_at(x, z, w).id;
}

const BiomeDefinition& TerrainGenerator::biome_definition_at(int x, int z, int w) const
{
    HYPERCRAFT_PROFILE_GENERATION_COUNT(biome_queries);
    const BiomeTileKey key {
        floor_div(x, biome_tile_size),
        floor_div(z, biome_tile_size),
        floor_div(w, biome_tile_size),
    };
    bool cache_hit = false;
    const auto tile = cache_state_->biome_tile(*this, key, &cache_hit);
    if (cache_hit) {
        HYPERCRAFT_PROFILE_GENERATION_COUNT(biome_cache_hits);
    } else {
        HYPERCRAFT_PROFILE_GENERATION_COUNT(biome_cache_misses);
    }
    return *tile->definitions[BiomeTile::index(
        floor_mod(x, biome_tile_size),
        floor_mod(z, biome_tile_size),
        floor_mod(w, biome_tile_size))];
}

int TerrainGenerator::height_at_4d(int x, int z, int w) const
{
    return *cache_state_->heights.get_or_compute(
        {x, z, w},
        [&] {
            for (int y = max_scan_y; y >= min_scan_y; --y) {
                if (raw_generated_solid({x, y, z, w})) {
                    return y;
                }
            }
            return min_scan_y;
        },
        [](int) { return sizeof(int); });
}

double TerrainGenerator::cave_noise_height(BlockCoord coord, int noise_index) const
{
    HYPERCRAFT_PROFILE_GENERATION_COUNT(cave_noise_calls);
    constexpr std::array<std::array<double, 4>, 3> offsets {{
        {170.0, -310.0, 520.0, -730.0},
        {-940.0, 410.0, -280.0, 660.0},
        {350.0, 880.0, -620.0, -140.0},
    }};
    const std::array<double, 4> offset = offsets[static_cast<std::size_t>(std::clamp(noise_index, 0, 2))];
    return fractal_noise_4d(
               static_cast<double>(coord.x) * cave_noise_scale + offset[0],
               static_cast<double>(coord.y) * cave_noise_scale + offset[1],
               static_cast<double>(coord.z) * cave_noise_scale + offset[2],
               static_cast<double>(coord.w) * cave_noise_scale + offset[3]) *
        cave_noise_height_scale;
}

bool TerrainGenerator::cave_carves_at(BlockCoord coord) const
{
    HYPERCRAFT_PROFILE_GENERATION_COUNT(cave_queries);
    return std::fabs(cave_noise_height(coord, 0)) <= cave_noise_brief_half_width &&
        std::fabs(cave_noise_height(coord, 1)) <= cave_noise_brief_half_width &&
        std::fabs(cave_noise_height(coord, 2)) <= cave_noise_brief_half_width;
}

BlockId TerrainGenerator::generated_block_at(BlockCoord coord) const
{
    if (raw_generated_solid(coord) && cave_carves_at(coord)) {
        return BlockId::air;
    }

    const BlockId base = base_block_at(coord);
    if (base != BlockId::air) {
        return base;
    }

    return air_or_fluid_at(coord);
}

BlockId TerrainGenerator::base_block_at(BlockCoord coord) const
{
    HYPERCRAFT_PROFILE_GENERATION_COUNT(base_block_queries);
    if (!raw_generated_solid(coord) || cave_carves_at(coord)) {
        return BlockId::air;
    }
    return classify_raw_solid_block_at(coord, biome_definition_at(coord.x, coord.z, coord.w));
}

BlockId TerrainGenerator::base_block_at(BlockCoord coord, const BiomeDefinition& biome) const
{
    HYPERCRAFT_PROFILE_GENERATION_COUNT(base_block_queries);
    if (!raw_generated_solid(coord) || cave_carves_at(coord)) {
        return BlockId::air;
    }
    return classify_raw_solid_block_at(coord, biome);
}

BlockId TerrainGenerator::classify_raw_solid_block_at(BlockCoord coord, const BiomeDefinition& biome) const
{
    if (!raw_generated_solid({coord.x, coord.y + 1, coord.z, coord.w})) {
        return surface_block_for_biome(biome, coord.y);
    }

    for (int offset = 1; offset <= 4; ++offset) {
        if (!raw_generated_solid({coord.x, coord.y + offset, coord.z, coord.w})) {
            return filler_block_for_biome(biome, offset - 1, coord.y);
        }
    }

    const BlockId ore = ore_patch_block_at(coord);
    return ore != BlockId::air && is_buried_ore_position(coord) ? ore : BlockId::stone;
}

bool TerrainGenerator::has_ocean_fill() const
{
    return generation_type_ != WorldGenerationType::overhang;
}

BlockId TerrainGenerator::air_or_fluid_at(BlockCoord coord) const
{
    HYPERCRAFT_PROFILE_GENERATION_COUNT(fluid_queries);
    if (!has_ocean_fill() || coord.y >= water_level) {
        return BlockId::air;
    }
    return fluid_block_for_biome(biome_definition_at(coord.x, coord.z, coord.w), coord.y);
}

BlockId TerrainGenerator::fluid_block_for_biome(const BiomeDefinition& definition, int y) const
{
    return y == water_level - 1 ? definition.water_top : definition.water_below;
}

BlockId TerrainGenerator::surface_block_for_biome(const BiomeDefinition& biome, int y) const
{
    HYPERCRAFT_PROFILE_GENERATION_COUNT(surface_block_queries);
    if (has_ocean_fill() && y < water_level) {
        return BlockId::dirt;
    }
    return biome.top_blocks[3];
}

BlockId TerrainGenerator::filler_block_for_biome(const BiomeDefinition& biome, int depth, int y) const
{
    HYPERCRAFT_PROFILE_GENERATION_COUNT(filler_block_queries);
    if (has_ocean_fill() && y < water_level) {
        return BlockId::dirt;
    }
    const int top_index = std::clamp(3 - depth, 0, 2);
    return biome.top_blocks[static_cast<std::size_t>(top_index)];
}

BlockId TerrainGenerator::ore_patch_block_at(BlockCoord coord) const
{
    HYPERCRAFT_PROFILE_GENERATION_COUNT(ore_queries);
    for (const OreGenerationRule& rule : ore_registry_->rules()) {
        HYPERCRAFT_PROFILE_GENERATION_COUNT(ore_rule_checks);
        const int cell_x = floor_div(coord.x, rule.spacing);
        const int cell_y = floor_div(coord.y, rule.spacing);
        const int cell_z = floor_div(coord.z, rule.spacing);
        const int cell_w = floor_div(coord.w, rule.spacing);
        const auto patch = ore_patch_for_cell(rule, cell_x, cell_y, cell_z, cell_w);
        if (!patch->active) {
            continue;
        }
        for (const BlockCoord offset : patch->offsets) {
            if (BlockCoord {
                    patch->center.x + offset.x,
                    patch->center.y + offset.y,
                    patch->center.z + offset.z,
                    patch->center.w + offset.w} == coord) {
                return rule.block;
            }
        }
    }

    return BlockId::air;
}

std::shared_ptr<const TerrainGenerator::CachedOrePatch> TerrainGenerator::ore_patch_for_cell(
    const OreGenerationRule& rule,
    int cell_x,
    int cell_y,
    int cell_z,
    int cell_w) const
{
    const OrePatchKey key {
        seed_,
        rule.salt,
        rule.spacing,
        rule.chance,
        rule.chance_denominator,
        rule.patch_radius,
        rule.min_count,
        rule.max_count,
        cell_x,
        cell_y,
        cell_z,
        cell_w,
    };
    bool cache_hit = false;
    const auto patch = cache_state_->ore_patches.get_or_compute(
        key,
        [&] {
            CachedOrePatch built;
            const std::uint32_t patch_hash =
                hash_lattice_4d(cell_x + rule.salt, cell_y, cell_z, cell_w, seed_);
            if (patch_hash % rule.chance_denominator >= rule.chance) {
                return built;
            }
            built.active = true;
            const std::uint32_t center_w_hash =
                hash_lattice_4d(cell_x, cell_y + rule.salt, cell_z, cell_w, seed_);
            const int inner_size = rule.spacing - rule.patch_radius * 2;
            built.center = {
                cell_x * rule.spacing + rule.patch_radius +
                    static_cast<int>((patch_hash >> 8u) % static_cast<std::uint32_t>(inner_size)),
                cell_y * rule.spacing + rule.patch_radius +
                    static_cast<int>((patch_hash >> 16u) % static_cast<std::uint32_t>(inner_size)),
                cell_z * rule.spacing + rule.patch_radius +
                    static_cast<int>((patch_hash >> 24u) % static_cast<std::uint32_t>(inner_size)),
                cell_w * rule.spacing + rule.patch_radius +
                    static_cast<int>((center_w_hash >> 10u) % static_cast<std::uint32_t>(inner_size)),
            };

            struct Candidate {
                std::uint32_t score = 0;
                BlockCoord offset {};
            };
            std::vector<Candidate> candidates;
            const int diameter = rule.patch_radius * 2 + 1;
            candidates.reserve(static_cast<std::size_t>(diameter * diameter * diameter * diameter));
            const int radius_squared = rule.patch_radius * rule.patch_radius;
            for (int ow = -rule.patch_radius; ow <= rule.patch_radius; ++ow) {
                for (int oy = -rule.patch_radius; oy <= rule.patch_radius; ++oy) {
                    for (int oz = -rule.patch_radius; oz <= rule.patch_radius; ++oz) {
                        for (int ox = -rule.patch_radius; ox <= rule.patch_radius; ++ox) {
                            const int distance = ox * ox + oy * oy + oz * oz + ow * ow;
                            if (distance > radius_squared) {
                                continue;
                            }
                            HYPERCRAFT_PROFILE_GENERATION_COUNT(ore_patch_candidates);
                            const BlockCoord candidate {
                                built.center.x + ox,
                                built.center.y + oy,
                                built.center.z + oz,
                                built.center.w + ow,
                            };
                            candidates.push_back({
                                static_cast<std::uint32_t>(distance) * 100000u +
                                    (hash_lattice_4d(
                                         candidate.x + rule.salt,
                                         candidate.y,
                                         candidate.z,
                                         candidate.w,
                                         seed_) %
                                     100000u),
                                {ox, oy, oz, ow},
                            });
                        }
                    }
                }
            }
            std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
                return left.score < right.score;
            });
            const int count_range = rule.max_count - rule.min_count + 1;
            const int selected_count = rule.min_count +
                static_cast<int>((patch_hash >> 3u) % static_cast<std::uint32_t>(count_range));
            const int count = std::min(selected_count, static_cast<int>(candidates.size()));
            built.offsets.reserve(static_cast<std::size_t>(count));
            for (int index = 0; index < count; ++index) {
                built.offsets.push_back(candidates[static_cast<std::size_t>(index)].offset);
            }
            return built;
        },
        [](const CachedOrePatch& value) {
            return sizeof(CachedOrePatch) + value.offsets.capacity() * sizeof(BlockCoord);
        },
        &cache_hit);
    if (cache_hit) {
        HYPERCRAFT_PROFILE_GENERATION_COUNT(ore_patch_cache_hits);
    } else {
        HYPERCRAFT_PROFILE_GENERATION_COUNT(ore_patch_cache_misses);
    }
    return patch;
}

void TerrainGenerator::stamp_ore_candidates(
    ChunkCoord coord,
    std::array<BlockId, chunk_volume>& candidates) const
{
    candidates.fill(BlockId::air);
    const int min_x = coord.x * chunk_size;
    const int min_y = coord.y * chunk_size;
    const int min_z = coord.z * chunk_size;
    const int min_w = coord.w * chunk_size;
    const int max_x = min_x + chunk_size - 1;
    const int max_y = min_y + chunk_size - 1;
    const int max_z = min_z + chunk_size - 1;
    const int max_w = min_w + chunk_size - 1;
    for (const OreGenerationRule& rule : ore_registry_->rules()) {
        for (int cell_w = floor_div(min_w, rule.spacing); cell_w <= floor_div(max_w, rule.spacing); ++cell_w) {
            for (int cell_y = floor_div(min_y, rule.spacing); cell_y <= floor_div(max_y, rule.spacing); ++cell_y) {
                for (int cell_z = floor_div(min_z, rule.spacing); cell_z <= floor_div(max_z, rule.spacing); ++cell_z) {
                    for (int cell_x = floor_div(min_x, rule.spacing); cell_x <= floor_div(max_x, rule.spacing); ++cell_x) {
                        HYPERCRAFT_PROFILE_GENERATION_COUNT(ore_rule_checks);
                        const auto patch = ore_patch_for_cell(rule, cell_x, cell_y, cell_z, cell_w);
                        if (!patch->active) {
                            continue;
                        }
                        for (const BlockCoord offset : patch->offsets) {
                            const BlockCoord block {
                                patch->center.x + offset.x,
                                patch->center.y + offset.y,
                                patch->center.z + offset.z,
                                patch->center.w + offset.w,
                            };
                            if (block.x < min_x || block.x > max_x ||
                                block.y < min_y || block.y > max_y ||
                                block.z < min_z || block.z > max_z ||
                                block.w < min_w || block.w > max_w) {
                                continue;
                            }
                            const std::size_t index = local_block_index({
                                block.x - min_x,
                                block.y - min_y,
                                block.z - min_z,
                                block.w - min_w,
                            });
                            if (candidates[index] == BlockId::air) {
                                candidates[index] = rule.block;
                            }
                        }
                    }
                }
            }
        }
    }
}

bool TerrainGenerator::is_buried_ore_position(BlockCoord coord) const
{
    return has_only_raw_solid_neighbors(coord, [this](BlockCoord neighbor) {
        return raw_generated_solid(neighbor);
    });
}

bool TerrainGenerator::surface_can_support_tree(
    BlockCoord coord,
    const BiomeDefinition& biome,
    const TreeDefinition& tree) const
{
    HYPERCRAFT_PROFILE_GENERATION_COUNT(tree_surface_queries);
    if (has_ocean_fill() && coord.y <= water_level + 1) {
        return false;
    }
    const BlockId surface = base_block_at(coord, biome);
    if (surface != surface_block_for_biome(biome, coord.y)) {
        return false;
    }
    const int trunk_height = tree_height_at(coord, tree);
    for (int level = 1; level <= trunk_height; ++level) {
        HYPERCRAFT_PROFILE_GENERATION_COUNT(tree_clearance_queries);
        if (base_block_at({coord.x, coord.y + level, coord.z, coord.w}, biome) != BlockId::air) {
            return false;
        }
    }
    return true;
}

std::uint32_t TerrainGenerator::tree_chance_roll(BlockCoord coord) const
{
    HYPERCRAFT_PROFILE_GENERATION_COUNT(tree_candidate_rolls);
    return hash_lattice_4d(coord.x, coord.y + 90210, coord.z, coord.w, seed_) % 10000u;
}

std::uint32_t TerrainGenerator::max_tree_chance() const
{
    std::uint32_t max_chance = 0;
    for (const BiomeDefinition& biome : biome_registry_->definitions()) {
        max_chance = std::max(max_chance, biome.tree_chance);
    }
    return max_chance;
}

int TerrainGenerator::tree_height_at(BlockCoord base, const TreeDefinition& tree) const
{
    const std::uint32_t height_hash = hash_lattice_4d(base.x, base.y + 12345, base.z, base.w, seed_) >> 8u;
    const int height_range = tree.max_trunk_height - tree.min_trunk_height + 1;
    return tree.min_trunk_height + static_cast<int>(height_hash % static_cast<std::uint32_t>(height_range));
}

int TerrainGenerator::tree_leaf_radius_squared(BlockCoord base, const TreeDefinition& tree) const
{
    const std::uint32_t radius_hash = hash_lattice_4d(base.x, base.y + 54321, base.z, base.w, seed_) >> 12u;
    const int radius_range = tree.max_leaf_radius - tree.min_leaf_radius + 1;
    return tree.min_leaf_radius + static_cast<int>(radius_hash % static_cast<std::uint32_t>(radius_range));
}

BlockCoord TerrainGenerator::tree_trunk_coord_at(BlockCoord base, int level, const TreeDefinition& tree) const
{
    BlockCoord coord {base.x, base.y + level, base.z, base.w};
    if (tree.trunk_shape == TrunkShape::straight) {
        return coord;
    }
    for (int step = 2; step <= level; ++step) {
        const std::uint32_t step_hash = hash_lattice_4d(base.x + 6673, base.y + step, base.z - 4153, base.w + 9209, seed_);
        switch (step_hash % 6u) {
        case 0:
            --coord.x;
            break;
        case 1:
            ++coord.x;
            break;
        case 2:
            --coord.z;
            break;
        case 3:
            ++coord.z;
            break;
        case 4:
            --coord.w;
            break;
        default:
            ++coord.w;
            break;
        }
    }
    return coord;
}

bool TerrainGenerator::tree_canopy_contains(
    const TreeDefinition& tree,
    BlockCoord top,
    int leaf_radius_squared,
    BlockCoord coord) const
{
    const int dx = coord.x - top.x;
    const int dy = coord.y - top.y;
    const int dz = coord.z - top.z;
    const int dw = coord.w - top.w;
    if (tree.leaf_shape == LeafShape::conal) {
        if (coord.y < top.y - leaf_radius_squared || coord.y > top.y + 1) {
            return false;
        }
        const int layer_from_tip = top.y + 1 - coord.y;
        const int radius = layer_from_tip == 0 ? 0 : std::min(leaf_radius_squared, layer_from_tip);
        return std::abs(dx) + std::abs(dz) + std::abs(dw) <= radius;
    }
    return dx * dx + dy * dy + dz * dz + dw * dw <= leaf_radius_squared;
}

Chunk TerrainGenerator::generate_chunk(ChunkCoord coord) const
{
    TerrainChunkGeneration generation = start_chunk_generation(coord);
    while (!generation.advance()) {
    }
    return generation.take_chunk();
}

TerrainChunkGeneration TerrainGenerator::start_chunk_generation(ChunkCoord coord) const
{
    HYPERCRAFT_PROFILE_GENERATION_SCOPE(coord, generation_type_);
    Chunk chunk(coord);
    ChunkGenerationScratch scratch;
    const int min_x = coord.x * chunk_size;
    const int min_y = coord.y * chunk_size;
    const int min_z = coord.z * chunk_size;
    const int min_w = coord.w * chunk_size;
    const int max_x = min_x + chunk_size - 1;
    const int max_y = min_y + chunk_size - 1;
    const int max_z = min_z + chunk_size - 1;
    const int max_w = min_w + chunk_size - 1;

    auto density_coordinates = regular_noise_coordinates(
        {min_x, min_y, min_z, min_w},
        {chunk_size, ChunkGenerationScratch::cached_y_size, chunk_size, chunk_size},
        {1.0, 1.0, 1.0, 1.0},
        {0.0, 50.0, 0.0, -50.0});
    for (int x = 0; x < chunk_size; ++x) {
        density_coordinates[0][static_cast<std::size_t>(x)] =
            static_cast<double>(min_x + x) * 0.045 / 3.0;
        density_coordinates[2][static_cast<std::size_t>(x)] =
            static_cast<double>(min_z + x) * 0.045 / 3.0;
        density_coordinates[3][static_cast<std::size_t>(x)] =
            static_cast<double>(min_w + x) * 0.045 / 3.0 - 50.0;
    }
    for (int y = 0; y < ChunkGenerationScratch::cached_y_size; ++y) {
        density_coordinates[1][static_cast<std::size_t>(y)] =
            static_cast<double>(min_y + y) * 0.075 / 3.0 + 50.0;
    }
    const RegularFractalNoise4D density_noise(seed_, density_coordinates);
    co_yield 0U;

    const RegularFractalNoise3D broad_height_noise(
        seed_,
        height_noise_coordinates(min_x, min_z, min_w, 0.035, {0.0, 0.0, 0.0}));
    const RegularFractalNoise3D small_height_noise(
        seed_,
        height_noise_coordinates(min_x, min_z, min_w, 0.11, {100.0, 50.0, -100.0}));
    co_yield 0U;

    constexpr std::array<std::array<double, 4>, 3> cave_offsets {{
        {170.0, -310.0, 520.0, -730.0},
        {-940.0, 410.0, -280.0, 660.0},
        {350.0, 880.0, -620.0, -140.0},
    }};
    std::array<std::unique_ptr<RegularFractalNoise4D>, 3> cave_noises;
    for (std::size_t index = 0; index < cave_noises.size(); ++index) {
        cave_noises[index] = std::make_unique<RegularFractalNoise4D>(
            seed_,
            regular_noise_coordinates(
                {min_x, min_y, min_z, min_w},
                {chunk_size, chunk_size, chunk_size, chunk_size},
                {cave_noise_scale, cave_noise_scale, cave_noise_scale, cave_noise_scale},
                cave_offsets[index]));
        co_yield 0U;
    }

    auto height_guide_for_local = [&](int x, int z, int w) {
        const std::size_t index = ChunkGenerationScratch::neighbor_column_index(x, z, w);
        if (!scratch.height_guide_known[index]) {
            scratch.height_guides[index] = height_guide_at(min_x + x, min_z + z, min_w + w);
            scratch.height_guide_known[index] = true;
        }
        return scratch.height_guides[index];
    };
    auto raw_solid_for_local = [&](int x, int y, int z, int w) {
        if (!scratch.raw_solid_is_known(x, y, z, w)) {
            const BlockCoord block_coord {min_x + x, min_y + y, min_z + z, min_w + w};
            scratch.set_raw_solid(
                x,
                y,
                z,
                w,
                raw_generated_solid_with_height_guide(block_coord, height_guide_for_local(x, z, w)));
        }
        return scratch.raw_solid_at(x, y, z, w);
    };

    HYPERCRAFT_PROFILE_GENERATION_STAGE(ChunkGenerationStage::biome_lookup);
    for (int tile_w = 0; tile_w < chunk_size; tile_w += biome_tile_size) {
        for (int tile_z = 0; tile_z < chunk_size; tile_z += biome_tile_size) {
            for (int tile_x = 0; tile_x < chunk_size; tile_x += biome_tile_size) {
                bool cache_hit = false;
                const auto tile = cache_state_->biome_tile(
                    *this,
                    {
                        floor_div(min_x + tile_x, biome_tile_size),
                        floor_div(min_z + tile_z, biome_tile_size),
                        floor_div(min_w + tile_w, biome_tile_size),
                    },
                    &cache_hit);
                if (cache_hit) {
                    HYPERCRAFT_PROFILE_GENERATION_COUNT(biome_cache_hits);
                } else {
                    HYPERCRAFT_PROFILE_GENERATION_COUNT(biome_cache_misses);
                }
                HYPERCRAFT_PROFILE_GENERATION_ADD(
                    biome_queries,
                    biome_tile_size * biome_tile_size * biome_tile_size);
                for (int local_w = 0; local_w < biome_tile_size; ++local_w) {
                    for (int local_z = 0; local_z < biome_tile_size; ++local_z) {
                        for (int local_x = 0; local_x < biome_tile_size; ++local_x) {
                            scratch.biomes[ChunkGenerationScratch::column_index(
                                tile_x + local_x,
                                tile_z + local_z,
                                tile_w + local_w)] =
                                tile->definitions[BiomeTile::index(local_x, local_z, local_w)];
                        }
                    }
                }
            }
        }
        co_yield 0U;
    }

    HYPERCRAFT_PROFILE_GENERATION_STAGE(ChunkGenerationStage::density_and_caves);
    for (int w = 0; w < chunk_size; ++w) {
        for (int z = 0; z < chunk_size; ++z) {
            for (int x = 0; x < chunk_size; ++x) {
                HYPERCRAFT_PROFILE_GENERATION_COUNT(height_guide_calls);
                const double broad_hills = broad_height_noise.sample(
                    static_cast<std::size_t>(x),
                    static_cast<std::size_t>(z),
                    static_cast<std::size_t>(w)) *
                    18.0;
                const double small_hills = small_height_noise.sample(
                    static_cast<std::size_t>(x),
                    static_cast<std::size_t>(z),
                    static_cast<std::size_t>(w)) *
                    5.0;
                scratch.height_guides[ChunkGenerationScratch::neighbor_column_index(x, z, w)] =
                    std::round(18.0 + broad_hills + small_hills);
                scratch.height_guide_known[ChunkGenerationScratch::neighbor_column_index(x, z, w)] = true;
            }
        }
        co_yield 0U;
    }
    for (int w = 0; w < chunk_size; ++w) {
        for (int y = 0; y < ChunkGenerationScratch::cached_y_size; ++y) {
            for (int z = 0; z < chunk_size; ++z) {
                for (int x = 0; x < chunk_size; ++x) {
                    const BlockCoord block_coord = block_coord_from_chunk_local(coord, {x, y, z, w});
                    HYPERCRAFT_PROFILE_GENERATION_COUNT(raw_solid_queries);
                    HYPERCRAFT_PROFILE_GENERATION_COUNT(density_calls);
                    const double base_height =
                        scratch.height_guides[ChunkGenerationScratch::neighbor_column_index(x, z, w)];
                    constexpr double normal_vertical_falloff = 0.045;
                    constexpr double normal_height_falloff = 0.35 / 90.0;
                    const double falloff_multiplier = world_generation_diminishing_multiplier(generation_type_);
                    const double vertical_falloff = normal_vertical_falloff * falloff_multiplier;
                    const double height_falloff = normal_height_falloff * falloff_multiplier;
                    const double world_y = static_cast<double>(block_coord.y);
                    const double above_base = std::max(0.0, world_y - base_height);
                    const double sampled_density =
                        (base_height + 1.0 - world_y) * vertical_falloff +
                        density_noise.sample(
                            static_cast<std::size_t>(x),
                            static_cast<std::size_t>(y),
                            static_cast<std::size_t>(z),
                            static_cast<std::size_t>(w)) *
                            16.0 -
                        above_base * height_falloff;
                    scratch.set_raw_solid(
                        x,
                        y,
                        z,
                        w,
                        sampled_density > 0.0);
                    if (y < chunk_size) {
                        bool cave_carves = false;
                        if (scratch.raw_solid_at(x, y, z, w)) {
                            HYPERCRAFT_PROFILE_GENERATION_COUNT(cave_queries);
                            cave_carves = true;
                            for (std::size_t cave_index = 0; cave_index < cave_noises.size(); ++cave_index) {
                                HYPERCRAFT_PROFILE_GENERATION_COUNT(cave_noise_calls);
                                const double cave_height = cave_noises[cave_index]->sample(
                                    static_cast<std::size_t>(x),
                                    static_cast<std::size_t>(y),
                                    static_cast<std::size_t>(z),
                                    static_cast<std::size_t>(w)) *
                                    cave_noise_height_scale;
                                if (std::fabs(cave_height) > cave_noise_brief_half_width) {
                                    cave_carves = false;
                                    break;
                                }
                            }
                        }
                        scratch.solid[ChunkGenerationScratch::solid_index(x, y, z, w)] =
                            scratch.raw_solid_at(x, y, z, w) && !cave_carves;
                    }
                }
            }
        }
        co_yield 0U;
    }

    HYPERCRAFT_PROFILE_GENERATION_STAGE(ChunkGenerationStage::ore_patches);
    std::array<BlockId, chunk_volume> ore_candidates;
    stamp_ore_candidates(coord, ore_candidates);
    co_yield 0U;

    HYPERCRAFT_PROFILE_GENERATION_STAGE(ChunkGenerationStage::block_classification);
    for (int w = 0; w < chunk_size; ++w) {
        for (int y = 0; y < chunk_size; ++y) {
            for (int z = 0; z < chunk_size; ++z) {
                for (int x = 0; x < chunk_size; ++x) {
                    const LocalBlockCoord local {x, y, z, w};
                    const BlockCoord block_coord = block_coord_from_chunk_local(coord, local);
                    const BiomeDefinition& biome = *scratch.biomes[ChunkGenerationScratch::column_index(x, z, w)];
                    if (scratch.raw_solid_at(x, y, z, w) &&
                        !scratch.solid[ChunkGenerationScratch::solid_index(x, y, z, w)]) {
                        chunk.set_block(local, BlockId::air);
                    } else if (!scratch.solid[ChunkGenerationScratch::solid_index(x, y, z, w)]) {
                        chunk.set_block(
                            local,
                            !has_ocean_fill() || block_coord.y >= water_level
                                ? BlockId::air
                                : fluid_block_for_biome(biome, block_coord.y));
                    } else if (!scratch.raw_solid_at(x, y + 1, z, w)) {
                        chunk.set_block(local, surface_block_for_biome(biome, block_coord.y));
                    } else {
                        BlockId block = BlockId::stone;
                        for (int offset = 1; offset <= ChunkGenerationScratch::classification_padding; ++offset) {
                            if (!scratch.raw_solid_at(x, y + offset, z, w)) {
                                block = filler_block_for_biome(biome, offset - 1, block_coord.y);
                                break;
                            }
                        }
                        if (block == BlockId::stone) {
                            HYPERCRAFT_PROFILE_GENERATION_COUNT(ore_queries);
                            const BlockId ore = ore_candidates[local_block_index(local)];
                            if (ore != BlockId::air && has_only_raw_solid_neighbors(
                                    block_coord,
                                    [&](BlockCoord neighbor) {
                                        return raw_solid_for_local(
                                            neighbor.x - min_x,
                                            neighbor.y - min_y,
                                            neighbor.z - min_z,
                                            neighbor.w - min_w);
                                    })) {
                                block = ore;
                            }
                        }
                        chunk.set_block(local, block);
                    }
                }
            }
        }
        co_yield 0U;
    }

    constexpr int tree_candidate_tile_size = 4;
    const int tree_leaf_search_radius = tree_registry_->max_leaf_search_radius();
    const std::uint32_t global_tree_chance = max_tree_chance();
    std::vector<std::shared_ptr<const TreeBase>> tree_bases;
    tree_bases.reserve(8);

    HYPERCRAFT_PROFILE_GENERATION_STAGE(ChunkGenerationStage::tree_candidates);
    if (global_tree_chance > 0u) {
        const int min_base_x = min_x - tree_leaf_search_radius;
        const int max_base_x = max_x + tree_leaf_search_radius;
        const int min_base_z = min_z - tree_leaf_search_radius;
        const int max_base_z = max_z + tree_leaf_search_radius;
        const int min_base_w = min_w - tree_leaf_search_radius;
        const int max_base_w = max_w + tree_leaf_search_radius;
        const int tree_search_x_size = max_base_x - min_base_x + 1;
        const int tree_search_z_size = max_base_z - min_base_z + 1;
        std::vector<const BiomeDefinition*> tree_biomes(
            static_cast<std::size_t>(tree_search_x_size * tree_search_z_size *
                                     (max_base_w - min_base_w + 1)),
            nullptr);
        const auto tree_biome_index = [&](int x, int z, int w) {
            return static_cast<std::size_t>(
                (x - min_base_x) + tree_search_x_size *
                    ((z - min_base_z) + tree_search_z_size * (w - min_base_w)));
        };
        for (int tile_w = floor_div(min_base_w, biome_tile_size);
             tile_w <= floor_div(max_base_w, biome_tile_size);
             ++tile_w) {
            for (int tile_z = floor_div(min_base_z, biome_tile_size);
                 tile_z <= floor_div(max_base_z, biome_tile_size);
                 ++tile_z) {
                for (int tile_x = floor_div(min_base_x, biome_tile_size);
                     tile_x <= floor_div(max_base_x, biome_tile_size);
                     ++tile_x) {
                    bool cache_hit = false;
                    const auto tile = cache_state_->biome_tile(*this, {tile_x, tile_z, tile_w}, &cache_hit);
                    if (cache_hit) {
                        HYPERCRAFT_PROFILE_GENERATION_COUNT(biome_cache_hits);
                    } else {
                        HYPERCRAFT_PROFILE_GENERATION_COUNT(biome_cache_misses);
                    }
                    const int tile_origin_x = tile_x * biome_tile_size;
                    const int tile_origin_z = tile_z * biome_tile_size;
                    const int tile_origin_w = tile_w * biome_tile_size;
                    for (int w = std::max(min_base_w, tile_origin_w);
                         w <= std::min(max_base_w, tile_origin_w + biome_tile_size - 1);
                         ++w) {
                        for (int z = std::max(min_base_z, tile_origin_z);
                             z <= std::min(max_base_z, tile_origin_z + biome_tile_size - 1);
                             ++z) {
                            for (int x = std::max(min_base_x, tile_origin_x);
                                 x <= std::min(max_base_x, tile_origin_x + biome_tile_size - 1);
                                 ++x) {
                                tree_biomes[tree_biome_index(x, z, w)] = tile->definitions[BiomeTile::index(
                                    x - tile_origin_x,
                                    z - tile_origin_z,
                                    w - tile_origin_w)];
                                HYPERCRAFT_PROFILE_GENERATION_COUNT(biome_queries);
                            }
                        }
                    }
                }
            }
            co_yield 0U;
        }
        const auto tree_biome_at = [&](int x, int z, int w) -> const BiomeDefinition& {
            return *tree_biomes[tree_biome_index(x, z, w)];
        };

        int min_base_y = min_y;
        int max_base_y = max_y;
        bool has_tree_definition = false;
        for (const TreeDefinition& definition : tree_registry_->definitions()) {
            if (definition.id == "none") {
                continue;
            }
            const TreeOverlapBounds bounds = tree_overlap_bounds(definition, min_y, max_y);
            min_base_y = has_tree_definition ? std::min(min_base_y, bounds.min_base_y) : bounds.min_base_y;
            max_base_y = has_tree_definition ? std::max(max_base_y, bounds.max_base_y) : bounds.max_base_y;
            has_tree_definition = true;
        }

        if (has_tree_definition) {
            const int min_tile_x = floor_div(min_base_x, tree_candidate_tile_size);
            const int max_tile_x = floor_div(max_base_x, tree_candidate_tile_size);
            const int min_tile_z = floor_div(min_base_z, tree_candidate_tile_size);
            const int max_tile_z = floor_div(max_base_z, tree_candidate_tile_size);
            const int min_tile_w = floor_div(min_base_w, tree_candidate_tile_size);
            const int max_tile_w = floor_div(max_base_w, tree_candidate_tile_size);
            for (int tile_w = min_tile_w; tile_w <= max_tile_w; ++tile_w) {
                for (int tile_z = min_tile_z; tile_z <= max_tile_z; ++tile_z) {
                    for (int tile_x = min_tile_x; tile_x <= max_tile_x; ++tile_x) {
                        bool tile_is_requested = false;
                        int requested_min_y = 0;
                        int requested_max_y = 0;
                        const int tile_origin_x = tile_x * tree_candidate_tile_size;
                        const int tile_origin_z = tile_z * tree_candidate_tile_size;
                        const int tile_origin_w = tile_w * tree_candidate_tile_size;
                        const int tile_min_x = std::max(min_base_x, tile_origin_x);
                        const int tile_max_x =
                            std::min(max_base_x, tile_origin_x + tree_candidate_tile_size - 1);
                        const int tile_min_z = std::max(min_base_z, tile_origin_z);
                        const int tile_max_z =
                            std::min(max_base_z, tile_origin_z + tree_candidate_tile_size - 1);
                        const int tile_min_w = std::max(min_base_w, tile_origin_w);
                        const int tile_max_w =
                            std::min(max_base_w, tile_origin_w + tree_candidate_tile_size - 1);
                        for (int base_w = tile_min_w; base_w <= tile_max_w; ++base_w) {
                            for (int base_z = tile_min_z; base_z <= tile_max_z; ++base_z) {
                                for (int base_x = tile_min_x; base_x <= tile_max_x; ++base_x) {
                                    const BiomeDefinition& biome = tree_biome_at(base_x, base_z, base_w);
                                    const TreeDefinition& definition = tree_registry_->definition(biome.tree_id);
                                    if (definition.id == "none" || biome.tree_chance == 0u) {
                                        continue;
                                    }
                                    const TreeOverlapBounds bounds = tree_overlap_bounds(definition, min_y, max_y);
                                    if (base_x < min_x - bounds.horizontal_extent ||
                                        base_x > max_x + bounds.horizontal_extent ||
                                        base_z < min_z - bounds.horizontal_extent ||
                                        base_z > max_z + bounds.horizontal_extent ||
                                        base_w < min_w - bounds.horizontal_extent ||
                                        base_w > max_w + bounds.horizontal_extent) {
                                        continue;
                                    }
                                    requested_min_y = tile_is_requested
                                        ? std::min(requested_min_y, bounds.min_base_y)
                                        : bounds.min_base_y;
                                    requested_max_y = tile_is_requested
                                        ? std::max(requested_max_y, bounds.max_base_y)
                                        : bounds.max_base_y;
                                    tile_is_requested = true;
                                }
                            }
                        }
                        if (!tile_is_requested) {
                            continue;
                        }

                        for (int tile_y = floor_div(requested_min_y, tree_candidate_tile_size);
                             tile_y <= floor_div(requested_max_y, tree_candidate_tile_size);
                             ++tile_y) {
                            const TreeCandidateTileKey tile_key {
                                seed_, tile_x, tile_y, tile_z, tile_w, global_tree_chance};
                            const auto tile = cache_state_->tree_candidates.get_or_compute(
                                tile_key,
                                [&] {
                                    TreeCandidateTile built;
                                    const int candidate_min_x = tile_key.x * tree_candidate_tile_size;
                                    const int candidate_min_y = tile_key.y * tree_candidate_tile_size;
                                    const int candidate_min_z = tile_key.z * tree_candidate_tile_size;
                                    const int candidate_min_w = tile_key.w * tree_candidate_tile_size;
                                    built.candidates.reserve(32);
                                    for (int candidate_w = candidate_min_w;
                                         candidate_w < candidate_min_w + tree_candidate_tile_size;
                                         ++candidate_w) {
                                        for (int candidate_y = candidate_min_y;
                                             candidate_y < candidate_min_y + tree_candidate_tile_size;
                                             ++candidate_y) {
                                            for (int candidate_z = candidate_min_z;
                                                 candidate_z < candidate_min_z + tree_candidate_tile_size;
                                                 ++candidate_z) {
                                                for (int candidate_x = candidate_min_x;
                                                     candidate_x < candidate_min_x + tree_candidate_tile_size;
                                                     ++candidate_x) {
                                                    const BlockCoord base {
                                                        candidate_x, candidate_y, candidate_z, candidate_w};
                                                    const std::uint32_t roll = tree_chance_roll(base);
                                                    if (roll < global_tree_chance) {
                                                        built.candidates.push_back({base, roll});
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    built.candidates.shrink_to_fit();
                                    return built;
                                },
                                [](const TreeCandidateTile& value) {
                                    return sizeof(TreeCandidateTile) +
                                        value.candidates.capacity() * sizeof(TreeCandidate);
                                });

                            for (const TreeCandidate& candidate : tile->candidates) {
                                const BlockCoord base = candidate.base;
                                if (base.x < min_base_x || base.x > max_base_x ||
                                    base.y < min_base_y || base.y > max_base_y ||
                                    base.z < min_base_z || base.z > max_base_z ||
                                    base.w < min_base_w || base.w > max_base_w) {
                                    continue;
                                }
                                const BiomeDefinition& biome = tree_biome_at(base.x, base.z, base.w);
                                const TreeDefinition& definition = tree_registry_->definition(biome.tree_id);
                                if (definition.id == "none" || biome.tree_chance == 0u) {
                                    continue;
                                }
                                const TreeOverlapBounds bounds = tree_overlap_bounds(definition, min_y, max_y);
                                if (base.x < min_x - bounds.horizontal_extent ||
                                    base.x > max_x + bounds.horizontal_extent ||
                                    base.z < min_z - bounds.horizontal_extent ||
                                    base.z > max_z + bounds.horizontal_extent ||
                                    base.w < min_w - bounds.horizontal_extent ||
                                    base.w > max_w + bounds.horizontal_extent) {
                                    continue;
                                }
                                if (base.y < bounds.min_base_y || base.y > bounds.max_base_y ||
                                    candidate.roll >= biome.tree_chance) {
                                    continue;
                                }

                                HYPERCRAFT_PROFILE_GENERATION_COUNT(tree_base_queries);
                                if (!surface_can_support_tree(base, biome, definition)) {
                                    continue;
                                }
                                bool cache_hit = false;
                                const auto tree = cache_state_->accepted_trees.get_or_compute(
                                    base,
                                    [&] {
                                        TreeBase built;
                                        built.base = base;
                                        built.definition = &definition;
                                        built.height = tree_height_at(base, definition);
                                        built.leaf_radius_squared = tree_leaf_radius_squared(base, definition);
                                        built.trunk.reserve(static_cast<std::size_t>(built.height));
                                        for (int level = 1; level <= built.height; ++level) {
                                            built.trunk.push_back(tree_trunk_coord_at(base, level, definition));
                                        }
                                        built.top = built.trunk.back();
                                        built.canopy_extent = definition.leaf_shape == LeafShape::conal
                                            ? built.leaf_radius_squared
                                            : spherical_leaf_extent(built.leaf_radius_squared);
                                        built.canopy_min_y = definition.leaf_shape == LeafShape::conal
                                            ? built.top.y - built.leaf_radius_squared
                                            : built.top.y - built.canopy_extent;
                                        built.canopy_max_y = definition.leaf_shape == LeafShape::conal
                                            ? built.top.y + 1
                                            : built.top.y + built.canopy_extent;
                                        return built;
                                    },
                                    [](const TreeBase& value) {
                                        return sizeof(TreeBase) + value.trunk.capacity() * sizeof(BlockCoord);
                                    },
                                    &cache_hit);
                                if (cache_hit) {
                                    HYPERCRAFT_PROFILE_GENERATION_COUNT(tree_base_cache_hits);
                                } else {
                                    HYPERCRAFT_PROFILE_GENERATION_COUNT(tree_base_cache_misses);
                                }
                                tree_bases.push_back(tree);
                                HYPERCRAFT_PROFILE_GENERATION_COUNT(accepted_tree_bases);
                            }
                            co_yield 0U;
                        }
                    }
                }
            }
        }
    }

    std::sort(tree_bases.begin(), tree_bases.end(), [](const auto& left_value, const auto& right_value) {
        const TreeBase& left = *left_value;
        const TreeBase& right = *right_value;
        if (left.base.w != right.base.w) {
            return left.base.w < right.base.w;
        }
        if (left.base.y != right.base.y) {
            return left.base.y < right.base.y;
        }
        if (left.base.z != right.base.z) {
            return left.base.z < right.base.z;
        }
        return left.base.x < right.base.x;
    });

    HYPERCRAFT_PROFILE_GENERATION_STAGE(ChunkGenerationStage::tree_application);
    for (const auto& tree_value : tree_bases) {
        const TreeBase& tree = *tree_value;
        const TreeDefinition& definition = *tree.definition;
        for (const BlockCoord trunk_coord : tree.trunk) {
            HYPERCRAFT_PROFILE_GENERATION_COUNT(tree_trunk_voxel_tests);
            if (
                trunk_coord.x < min_x || trunk_coord.x > max_x ||
                trunk_coord.y < min_y || trunk_coord.y > max_y ||
                trunk_coord.z < min_z || trunk_coord.z > max_z ||
                trunk_coord.w < min_w || trunk_coord.w > max_w
            ) {
                continue;
            }
            const LocalBlockCoord local {
                trunk_coord.x - min_x,
                trunk_coord.y - min_y,
                trunk_coord.z - min_z,
                trunk_coord.w - min_w,
            };
            if (chunk.block_at(local) == BlockId::air) {
                chunk.set_block(local, definition.trunk_material);
            }
        }

        for (int w = tree.top.w - tree.canopy_extent; w <= tree.top.w + tree.canopy_extent; ++w) {
            if (w < min_w || w > max_w) {
                continue;
            }
            for (int z = tree.top.z - tree.canopy_extent; z <= tree.top.z + tree.canopy_extent; ++z) {
                if (z < min_z || z > max_z) {
                    continue;
                }
                for (int y = tree.canopy_min_y; y <= tree.canopy_max_y; ++y) {
                    if (y < min_y || y > max_y) {
                        continue;
                    }
                    for (int x = tree.top.x - tree.canopy_extent; x <= tree.top.x + tree.canopy_extent; ++x) {
                        if (x < min_x || x > max_x) {
                            continue;
                        }
                        HYPERCRAFT_PROFILE_GENERATION_COUNT(tree_canopy_voxel_tests);
                        if (!tree_canopy_contains(definition, tree.top, tree.leaf_radius_squared, {x, y, z, w})) {
                            continue;
                        }
                        const LocalBlockCoord local {x - min_x, y - min_y, z - min_z, w - min_w};
                        if (chunk.block_at(local) == BlockId::air) {
                            chunk.set_block(local, definition.leaf_material);
                        }
                    }
                }
            }
        }
        co_yield 0U;
    }

    co_return chunk;
}

} // namespace hypercraft
