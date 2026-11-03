#include "hypercraft/world/world_seed.hpp"

#include <chrono>
#include <random>

namespace hypercraft {

std::uint32_t random_world_seed()
{
    std::random_device random_device;
    const auto now = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const std::uint64_t entropy =
        (static_cast<std::uint64_t>(random_device()) << 32u) ^
        static_cast<std::uint64_t>(random_device()) ^
        now;
    std::seed_seq seed_sequence {
        static_cast<std::uint32_t>(entropy),
        static_cast<std::uint32_t>(entropy >> 32u),
        static_cast<std::uint32_t>(now),
        static_cast<std::uint32_t>(now >> 32u),
    };
    std::uint32_t seed = 0;
    seed_sequence.generate(&seed, &seed + 1);
    return seed;
}

} // namespace hypercraft
