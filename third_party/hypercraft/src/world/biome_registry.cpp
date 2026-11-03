#include "hypercraft/world/biome_registry.hpp"

#include "hypercraft/world/tree_registry.hpp"

#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace hypercraft {
namespace {

constexpr std::string_view default_biomes_lua = R"lua(
return {
  {
    id = "tundra",
    temperature = { min = 0.0, max = 0.478 },
    precipitation = { min = 0.0, max = 0.515 },
    top_blocks = { "dirt", "dirt", "snow", "snow" },
    water = { below = "water", top = "ice" },
    tree = "none",
    tree_chance = 0,
  },
  {
    id = "windswept",
    temperature = { min = 0.350, max = 0.478 },
    precipitation = { min = 0.0, max = 0.300 },
    top_blocks = { "dirt", "dirt", "dirt", "orange_grass" },
    water = { below = "water", top = "water" },
    tree = "windswept",
    tree_chance = 48,
  },
  {
    id = "taiga",
    temperature = { min = 0.0, max = 0.478 },
    precipitation = { min = 0.515, max = 1.0 },
    top_blocks = { "dirt", "dirt", "snow", "snow" },
    water = { below = "water", top = "ice" },
    tree = "pine",
    tree_chance = 240,
  },
  {
    id = "plains",
    temperature = { min = 0.478, max = 0.522 },
    precipitation = { min = 0.0, max = 0.515 },
    top_blocks = { "dirt", "dirt", "dirt", "grass" },
    water = { below = "water", top = "water" },
    tree = "none",
    tree_chance = 0,
  },
  {
    id = "forest",
    temperature = { min = 0.478, max = 0.522 },
    precipitation = { min = 0.515, max = 1.0 },
    top_blocks = { "dirt", "dirt", "dirt", "grass" },
    water = { below = "water", top = "water" },
    tree = "oak",
    tree_chance = 240,
  },
  {
    id = "desert",
    temperature = { min = 0.522, max = 1.0 },
    precipitation = { min = 0.0, max = 0.515 },
    top_blocks = { "sand", "sand", "sand", "sand" },
    water = { below = "water", top = "water" },
    tree = "none",
    tree_chance = 0,
  },
  {
    id = "rainforest",
    temperature = { min = 0.522, max = 1.0 },
    precipitation = { min = 0.515, max = 1.0 },
    top_blocks = { "dirt", "dirt", "dirt", "grass" },
    water = { below = "water", top = "water" },
    tree = "jungle",
    tree_chance = 480,
  },
}
)lua";

constexpr int biome_count = 6;

std::size_t biome_index(Biome biome)
{
    return static_cast<std::size_t>(biome);
}

double climate_area(const BiomeDefinition& definition)
{
    return (definition.max_temperature - definition.min_temperature) *
        (definition.max_precipitation - definition.min_precipitation);
}

bool is_valid_custom_id(std::string_view id)
{
    if (id.empty()) {
        return false;
    }
    for (const char character : id) {
        const unsigned char value = static_cast<unsigned char>(character);
        if (!std::isalnum(value) && character != '_') {
            return false;
        }
    }
    return true;
}

BlockId world_block_id_from_name(const ContentRegistry& content_registry, std::string_view name)
{
    const BlockId block = content_registry.id_from_name(name);
    if (block != BlockId::air && !content_registry.definition(block).renderable) {
        throw std::runtime_error("unknown block id: " + std::string(name));
    }
    return block;
}

bool upper_bound_matches(double value, double max)
{
    return value < max || max >= 1.0;
}

std::uint32_t default_tree_chance(std::string_view tree_id)
{
    if (tree_id == "none") {
        return 0u;
    }
    if (tree_id == "jungle") {
        return 480u;
    }
    return 240u;
}

std::string read_text_file(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file) {
        return {};
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string load_biomes_source()
{
    const std::array<std::filesystem::path, 4> candidates {
#ifdef HYPERCRAFT_ASSET_DIR
        std::filesystem::path(HYPERCRAFT_ASSET_DIR) / "scripts" / "biomes.lua",
#else
        std::filesystem::path("/assets/scripts/biomes.lua"),
#endif
        std::filesystem::path("assets/scripts/biomes.lua"),
        std::filesystem::path("../assets/scripts/biomes.lua"),
        std::filesystem::path("../../assets/scripts/biomes.lua"),
    };

    for (const std::filesystem::path& path : candidates) {
        const std::string source = read_text_file(path);
        if (!source.empty()) {
            return source;
        }
    }
    return std::string(default_biomes_lua);
}

class BiomeLuaParser {
public:
    BiomeLuaParser(std::string_view source, const ContentRegistry& content_registry)
        : source_(source)
        , content_registry_(content_registry)
    {
    }

    std::vector<BiomeDefinition> parse_definitions()
    {
        std::vector<BiomeDefinition> definitions;
        std::array<bool, biome_count> seen {};
        std::unordered_set<std::string> ids;
        expect_identifier("return");
        expect('{');
        while (!consume('}')) {
            BiomeDefinition definition = parse_biome();
            if (!ids.insert(definition.id).second) {
                fail("duplicate biome id");
            }
            if (definition.builtin_biome.has_value()) {
                seen[biome_index(*definition.builtin_biome)] = true;
            }
            definitions.push_back(definition);
            consume(',');
        }
        for (const bool found : seen) {
            if (!found) {
                fail("biomes.lua must define every built-in biome");
            }
        }
        skip_space();
        if (position_ != source_.size()) {
            fail("unexpected trailing content");
        }
        return definitions;
    }

private:
    BiomeDefinition parse_biome()
    {
        BiomeDefinition definition;
        bool saw_id = false;
        bool saw_temperature = false;
        bool saw_precipitation = false;
        bool saw_top_blocks = false;
        bool saw_water = false;
        bool saw_tree = false;
        bool saw_tree_chance = false;

        expect('{');
        while (!consume('}')) {
            const std::string key = parse_identifier();
            expect('=');
            if (key == "id") {
                definition.id = parse_string();
                if (!is_valid_custom_id(definition.id)) {
                    fail("biome id must use only letters, digits, and underscores");
                }
                definition.builtin_biome = builtin_biome_from_id(definition.id);
                saw_id = true;
            } else if (key == "temperature") {
                parse_range(definition.min_temperature, definition.max_temperature);
                saw_temperature = true;
            } else if (key == "precipitation") {
                parse_range(definition.min_precipitation, definition.max_precipitation);
                saw_precipitation = true;
            } else if (key == "top_blocks") {
                definition.top_blocks = parse_top_blocks();
                saw_top_blocks = true;
            } else if (key == "water") {
                parse_water(definition.water_below, definition.water_top);
                saw_water = true;
            } else if (key == "tree") {
                definition.tree_id = parse_string();
                (void)TreeRegistry::default_registry().definition(definition.tree_id);
                saw_tree = true;
            } else if (key == "tree_chance") {
                definition.tree_chance = parse_integer();
                saw_tree_chance = true;
            } else {
                fail("unknown biome key: " + key);
            }
            consume(',');
        }

        if (!saw_id || !saw_temperature || !saw_precipitation || !saw_top_blocks || !saw_water || !saw_tree) {
            fail("biome is missing a required field");
        }
        validate_range(definition.min_temperature, definition.max_temperature, "temperature");
        validate_range(definition.min_precipitation, definition.max_precipitation, "precipitation");
        if (!saw_tree_chance) {
            definition.tree_chance = default_tree_chance(definition.tree_id);
        }
        if (definition.tree_chance > 10000u) {
            fail("tree_chance must be between 0 and 10000");
        }
        return definition;
    }

    void parse_range(double& min_value, double& max_value)
    {
        bool saw_min = false;
        bool saw_max = false;
        expect('{');
        while (!consume('}')) {
            const std::string key = parse_identifier();
            expect('=');
            if (key == "min") {
                min_value = parse_number();
                saw_min = true;
            } else if (key == "max") {
                max_value = parse_number();
                saw_max = true;
            } else {
                fail("unknown range key: " + key);
            }
            consume(',');
        }
        if (!saw_min || !saw_max) {
            fail("range requires min and max");
        }
    }

    std::array<BlockId, 4> parse_top_blocks()
    {
        std::array<BlockId, 4> blocks {};
        expect('{');
        for (std::size_t index = 0; index < blocks.size(); ++index) {
            blocks[index] = world_block_id_from_name(content_registry_, parse_string());
            if (index + 1 < blocks.size()) {
                expect(',');
            } else {
                consume(',');
            }
        }
        expect('}');
        return blocks;
    }

    void parse_water(BlockId& below, BlockId& top)
    {
        bool saw_below = false;
        bool saw_top = false;
        expect('{');
        while (!consume('}')) {
            const std::string key = parse_identifier();
            expect('=');
            if (key == "below") {
                below = world_block_id_from_name(content_registry_, parse_string());
                saw_below = true;
            } else if (key == "top") {
                top = world_block_id_from_name(content_registry_, parse_string());
                saw_top = true;
            } else {
                fail("unknown water key: " + key);
            }
            consume(',');
        }
        if (!saw_below || !saw_top) {
            fail("water requires below and top");
        }
    }

    static void validate_range(double min_value, double max_value, std::string_view name)
    {
        if (min_value < 0.0 || max_value > 1.0 || min_value >= max_value) {
            throw std::runtime_error(std::string(name) + " range must satisfy 0 <= min < max <= 1");
        }
    }

    bool consume(char expected)
    {
        skip_space();
        if (position_ < source_.size() && source_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void expect(char expected)
    {
        if (!consume(expected)) {
            fail(std::string("expected '") + expected + "'");
        }
    }

    void expect_identifier(std::string_view expected)
    {
        const std::string identifier = parse_identifier();
        if (identifier != expected) {
            fail("expected " + std::string(expected));
        }
    }

    std::string parse_identifier()
    {
        skip_space();
        if (position_ >= source_.size() || (!std::isalpha(as_uchar(source_[position_])) && source_[position_] != '_')) {
            fail("expected identifier");
        }
        const std::size_t start = position_;
        while (
            position_ < source_.size() &&
            (std::isalnum(as_uchar(source_[position_])) || source_[position_] == '_')
        ) {
            ++position_;
        }
        return std::string(source_.substr(start, position_ - start));
    }

    std::string parse_string()
    {
        skip_space();
        expect('"');
        std::string value;
        while (position_ < source_.size() && source_[position_] != '"') {
            if (source_[position_] == '\\') {
                fail("escape sequences are not allowed in biome strings");
            }
            value.push_back(source_[position_]);
            ++position_;
        }
        expect('"');
        return value;
    }

    double parse_number()
    {
        skip_space();
        const std::size_t start = position_;
        if (position_ < source_.size() && (source_[position_] == '-' || source_[position_] == '+')) {
            ++position_;
        }
        while (position_ < source_.size() && std::isdigit(as_uchar(source_[position_]))) {
            ++position_;
        }
        if (position_ < source_.size() && source_[position_] == '.') {
            ++position_;
            while (position_ < source_.size() && std::isdigit(as_uchar(source_[position_]))) {
                ++position_;
            }
        }
        if (start == position_) {
            fail("expected number");
        }
        const std::string text(source_.substr(start, position_ - start));
        char* end = nullptr;
        const double value = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || *end != '\0') {
            fail("invalid number");
        }
        return value;
    }

    std::uint32_t parse_integer()
    {
        skip_space();
        const std::size_t start = position_;
        while (position_ < source_.size() && std::isdigit(as_uchar(source_[position_]))) {
            ++position_;
        }
        if (start == position_) {
            fail("expected integer");
        }
        const std::string text(source_.substr(start, position_ - start));
        char* end = nullptr;
        const unsigned long value = std::strtoul(text.c_str(), &end, 10);
        if (end == text.c_str() || *end != '\0') {
            fail("invalid integer");
        }
        return static_cast<std::uint32_t>(value);
    }

    void skip_space()
    {
        while (position_ < source_.size()) {
            if (std::isspace(as_uchar(source_[position_]))) {
                ++position_;
                continue;
            }
            if (source_[position_] == '-' && position_ + 1 < source_.size() && source_[position_ + 1] == '-') {
                position_ += 2;
                while (position_ < source_.size() && source_[position_] != '\n') {
                    ++position_;
                }
                continue;
            }
            break;
        }
    }

    [[noreturn]] void fail(const std::string& message) const
    {
        throw std::runtime_error("biomes.lua parse error at byte " + std::to_string(position_) + ": " + message);
    }

    static unsigned char as_uchar(char value)
    {
        return static_cast<unsigned char>(value);
    }

    std::string_view source_;
    const ContentRegistry& content_registry_;
    std::size_t position_ = 0;
};

} // namespace

const char* biome_id(Biome biome)
{
    switch (biome) {
    case Biome::plains:
        return "plains";
    case Biome::forest:
        return "forest";
    case Biome::rainforest:
        return "rainforest";
    case Biome::desert:
        return "desert";
    case Biome::taiga:
        return "taiga";
    case Biome::tundra:
        return "tundra";
    }
    return "plains";
}

std::optional<Biome> builtin_biome_from_id(std::string_view id)
{
    if (id == "plains") {
        return Biome::plains;
    }
    if (id == "forest") {
        return Biome::forest;
    }
    if (id == "rainforest") {
        return Biome::rainforest;
    }
    if (id == "desert") {
        return Biome::desert;
    }
    if (id == "taiga") {
        return Biome::taiga;
    }
    if (id == "tundra") {
        return Biome::tundra;
    }
    return std::nullopt;
}

BiomeRegistry BiomeRegistry::from_lua_source(std::string_view source)
{
    return from_lua_source(source, ContentRegistry::default_registry());
}

BiomeRegistry BiomeRegistry::from_lua_source(std::string_view source, const ContentRegistry& content_registry)
{
    BiomeRegistry registry;
    registry.definitions_ = BiomeLuaParser(source, content_registry).parse_definitions();
    registry.builtin_indices_.fill(std::numeric_limits<std::size_t>::max());
    for (std::size_t index = 0; index < registry.definitions_.size(); ++index) {
        const std::optional<Biome> builtin = registry.definitions_[index].builtin_biome;
        if (builtin.has_value()) {
            registry.builtin_indices_[biome_index(*builtin)] = index;
        }
    }
    return registry;
}

const BiomeRegistry& BiomeRegistry::default_registry()
{
    static const BiomeRegistry registry = BiomeRegistry::from_lua_source(load_biomes_source());
    return registry;
}

const BiomeDefinition& BiomeRegistry::definition(Biome biome) const
{
    const std::size_t index = builtin_indices_[biome_index(biome)];
    if (index >= definitions_.size()) {
        throw std::runtime_error("missing built-in biome definition");
    }
    return definitions_[index];
}

const BiomeDefinition& BiomeRegistry::definition_for_climate(double temperature, double precipitation) const
{
    const BiomeDefinition* best = nullptr;
    double best_area = std::numeric_limits<double>::infinity();
    for (const BiomeDefinition& definition : definitions_) {
        if (
            temperature >= definition.min_temperature &&
            upper_bound_matches(temperature, definition.max_temperature) &&
            precipitation >= definition.min_precipitation &&
            upper_bound_matches(precipitation, definition.max_precipitation)
        ) {
            const double area = climate_area(definition);
            if (best == nullptr || area < best_area) {
                best = &definition;
                best_area = area;
            }
        }
    }
    if (best != nullptr) {
        return *best;
    }
    throw std::runtime_error("no biome matches generated climate values");
}

Biome BiomeRegistry::biome_for_climate(double temperature, double precipitation) const
{
    const BiomeDefinition& definition = definition_for_climate(temperature, precipitation);
    if (!definition.builtin_biome.has_value()) {
        throw std::runtime_error("matching biome has no built-in enum value: " + definition.id);
    }
    return *definition.builtin_biome;
}

const std::vector<BiomeDefinition>& BiomeRegistry::definitions() const
{
    return definitions_;
}

} // namespace hypercraft
