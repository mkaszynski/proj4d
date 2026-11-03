#include "hypercraft/world/ore_registry.hpp"

#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace hypercraft {
namespace {

constexpr std::string_view default_ores_lua = R"lua(
return {
  { block = "coal_ore", spacing = 8, chance = 300, chance_denominator = 1000, salt = 44004, patch_radius = 2, count = { min = 4, max = 10 } },
  { block = "tin_ore", spacing = 8, chance = 300, chance_denominator = 1000, salt = 11001, patch_radius = 2, count = { min = 4, max = 10 } },
  { block = "copper_ore", spacing = 8, chance = 200, chance_denominator = 1000, salt = 22002, patch_radius = 2, count = { min = 4, max = 10 } },
  { block = "water", spacing = 8, chance = 150, chance_denominator = 1000, salt = 55005, patch_radius = 2, count = { min = 10, max = 20 } },
  { block = "iron_ore", spacing = 8, chance = 100, chance_denominator = 1000, salt = 33003, patch_radius = 2, count = { min = 4, max = 10 } },
  { block = "lava", spacing = 8, chance = 150, chance_denominator = 1000, salt = 66006, patch_radius = 2, count = { min = 10, max = 20 } },
}
)lua";

BlockId world_block_id_from_name(const ContentRegistry& content_registry, std::string_view name)
{
    const BlockId block = content_registry.id_from_name(name);
    if (block == BlockId::air || !content_registry.definition(block).renderable) {
        throw std::runtime_error("unknown ore world block id: " + std::string(name));
    }
    return block;
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

std::string load_ores_source()
{
    const std::array<std::filesystem::path, 4> candidates {
#ifdef HYPERCRAFT_ASSET_DIR
        std::filesystem::path(HYPERCRAFT_ASSET_DIR) / "scripts" / "ores.lua",
#else
        std::filesystem::path("/assets/scripts/ores.lua"),
#endif
        std::filesystem::path("assets/scripts/ores.lua"),
        std::filesystem::path("../assets/scripts/ores.lua"),
        std::filesystem::path("../../assets/scripts/ores.lua"),
    };

    for (const std::filesystem::path& path : candidates) {
        const std::string source = read_text_file(path);
        if (!source.empty()) {
            return source;
        }
    }
    return std::string(default_ores_lua);
}

class OreLuaParser {
public:
    OreLuaParser(std::string_view source, const ContentRegistry& content_registry)
        : source_(source)
        , content_registry_(content_registry)
    {
    }

    std::vector<OreGenerationRule> parse_rules()
    {
        std::vector<OreGenerationRule> rules;
        expect_identifier("return");
        expect('{');
        while (!consume('}')) {
            rules.push_back(parse_rule());
            consume(',');
        }
        if (rules.empty()) {
            fail("ores.lua must define at least one ore rule");
        }
        skip_space();
        if (position_ != source_.size()) {
            fail("unexpected trailing content");
        }
        return rules;
    }

private:
    OreGenerationRule parse_rule()
    {
        OreGenerationRule rule;
        bool saw_block = false;
        bool saw_spacing = false;
        bool saw_chance = false;
        bool saw_chance_denominator = false;
        bool saw_salt = false;
        bool saw_patch_radius = false;
        bool saw_count = false;

        expect('{');
        while (!consume('}')) {
            const std::string key = parse_identifier();
            expect('=');
            if (key == "block") {
                rule.block = world_block_id_from_name(content_registry_, parse_string());
                saw_block = true;
            } else if (key == "spacing") {
                rule.spacing = parse_int();
                saw_spacing = true;
            } else if (key == "chance") {
                const int chance = parse_int();
                if (chance < 0) {
                    fail("chance must be non-negative");
                }
                rule.chance = static_cast<std::uint32_t>(chance);
                saw_chance = true;
            } else if (key == "chance_denominator") {
                const int chance_denominator = parse_int();
                if (chance_denominator <= 0) {
                    fail("chance_denominator must be positive");
                }
                rule.chance_denominator = static_cast<std::uint32_t>(chance_denominator);
                saw_chance_denominator = true;
            } else if (key == "salt") {
                rule.salt = parse_int();
                saw_salt = true;
            } else if (key == "patch_radius") {
                rule.patch_radius = parse_int();
                saw_patch_radius = true;
            } else if (key == "count") {
                parse_count_range(rule.min_count, rule.max_count);
                saw_count = true;
            } else {
                fail("unknown ore key: " + key);
            }
            consume(',');
        }

        if (
            !saw_block ||
            !saw_spacing ||
            !saw_chance ||
            !saw_chance_denominator ||
            !saw_salt ||
            !saw_patch_radius ||
            !saw_count
        ) {
            fail("ore rule is missing a required field");
        }
        if (rule.spacing <= 0) {
            fail("spacing must be positive");
        }
        if (rule.patch_radius < 0) {
            fail("patch_radius must be non-negative");
        }
        if (rule.spacing <= rule.patch_radius * 2) {
            fail("spacing must be larger than twice patch_radius");
        }
        if (rule.chance > rule.chance_denominator) {
            fail("chance must be between 0 and chance_denominator");
        }
        if (rule.min_count < 1) {
            fail("count.min must be at least 1");
        }
        if (rule.max_count < rule.min_count) {
            fail("count.max must be at least count.min");
        }
        return rule;
    }

    void parse_count_range(int& min_count, int& max_count)
    {
        bool saw_min = false;
        bool saw_max = false;
        expect('{');
        while (!consume('}')) {
            const std::string key = parse_identifier();
            expect('=');
            if (key == "min") {
                min_count = parse_int();
                saw_min = true;
            } else if (key == "max") {
                max_count = parse_int();
                saw_max = true;
            } else {
                fail("unknown ore count key: " + key);
            }
            consume(',');
        }
        if (!saw_min || !saw_max) {
            fail("ore count requires min and max");
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
                fail("escape sequences are not allowed in ore strings");
            }
            value.push_back(source_[position_]);
            ++position_;
        }
        expect('"');
        return value;
    }

    int parse_int()
    {
        skip_space();
        const std::size_t start = position_;
        if (position_ < source_.size() && (source_[position_] == '+' || source_[position_] == '-')) {
            ++position_;
        }
        while (position_ < source_.size() && std::isdigit(as_uchar(source_[position_]))) {
            ++position_;
        }
        if (start == position_) {
            fail("expected integer");
        }
        const std::string text(source_.substr(start, position_ - start));
        char* end = nullptr;
        const long value = std::strtol(text.c_str(), &end, 10);
        if (end == text.c_str() || *end != '\0') {
            fail("invalid integer");
        }
        return static_cast<int>(value);
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
        throw std::runtime_error("ores.lua parse error at byte " + std::to_string(position_) + ": " + message);
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

OreRegistry OreRegistry::from_lua_source(std::string_view source)
{
    return from_lua_source(source, ContentRegistry::default_registry());
}

OreRegistry OreRegistry::from_lua_source(std::string_view source, const ContentRegistry& content_registry)
{
    OreRegistry registry;
    registry.rules_ = OreLuaParser(source, content_registry).parse_rules();
    return registry;
}

const OreRegistry& OreRegistry::default_registry()
{
    static const OreRegistry registry = OreRegistry::from_lua_source(load_ores_source());
    return registry;
}

const std::vector<OreGenerationRule>& OreRegistry::rules() const
{
    return rules_;
}

const std::vector<OreGenerationRule>& ore_generation_rules()
{
    return OreRegistry::default_registry().rules();
}

} // namespace hypercraft
