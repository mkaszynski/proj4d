#include "hypercraft/render/texture_atlas.hpp"

#include "hypercraft/world/content_registry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#if HYPERCRAFT_USE_LIBPNG
#include <png.h>
#endif

namespace hypercraft {
namespace {

constexpr int crack_stage_count = 8;
constexpr float uv_inset = 0.5F;
constexpr int atlas_height = texture_tile_size;

struct Rgba {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

int clamp_channel(double value)
{
    return std::clamp(static_cast<int>(std::round(value)), 0, 255);
}

double noise(int x, int y, int seed)
{
    std::uint32_t value = static_cast<std::uint32_t>(x + 17) * 374761393u
        + static_cast<std::uint32_t>(y + 31) * 668265263u
        + static_cast<std::uint32_t>(seed) * 1442695041u;
    value = (value ^ (value >> 13u)) * 1274126177u;
    return static_cast<double>((value ^ (value >> 16u))) / 4294967295.0;
}

double signed_noise(int x, int y, int seed, double amount)
{
    return (noise(x, y, seed) * 2.0 - 1.0) * amount;
}

double smooth_noise(int x, int y, int seed, double amount)
{
    const double center = signed_noise(x, y, seed, amount);
    const double left = signed_noise(x - 1, y, seed, amount);
    const double right = signed_noise(x + 1, y, seed, amount);
    const double up = signed_noise(x, y - 1, seed, amount);
    const double down = signed_noise(x, y + 1, seed, amount);
    return center * 0.45 + (left + right + up + down) * 0.1375;
}

Rgba rgba(double r, double g, double b, double a = 255.0)
{
    return {
        static_cast<std::uint8_t>(clamp_channel(r)),
        static_cast<std::uint8_t>(clamp_channel(g)),
        static_cast<std::uint8_t>(clamp_channel(b)),
        static_cast<std::uint8_t>(clamp_channel(a)),
    };
}

std::vector<BlockId> block_texture_order()
{
    std::vector<BlockId> blocks;
    const auto& definitions = ContentRegistry::default_registry().definitions();
    blocks.reserve(definitions.size());
    for (const ContentDefinition& definition : definitions) {
        if (definition.id != BlockId::air) {
            blocks.push_back(definition.id);
        }
    }
    return blocks;
}

void assign_texture_regions(TextureAtlas& atlas)
{
    const std::vector<BlockId> blocks = block_texture_order();
    for (std::size_t tile = 0; tile < blocks.size(); ++tile) {
        const BlockId block = blocks[tile];
        atlas.blocks[static_cast<std::size_t>(block)] = {
            (static_cast<float>(tile * texture_tile_size) + uv_inset) / static_cast<float>(atlas.width),
            uv_inset / static_cast<float>(atlas.height),
            (static_cast<float>((tile + 1) * texture_tile_size) - uv_inset) / static_cast<float>(atlas.width),
            (static_cast<float>(atlas.height) - uv_inset) / static_cast<float>(atlas.height),
        };
    }
    for (int stage = 0; stage < crack_stage_count; ++stage) {
        const int tile = static_cast<int>(blocks.size()) + stage;
        atlas.cracks[static_cast<std::size_t>(stage)] = {
            (static_cast<float>(tile * texture_tile_size) + uv_inset) / static_cast<float>(atlas.width),
            uv_inset / static_cast<float>(atlas.height),
            (static_cast<float>((tile + 1) * texture_tile_size) - uv_inset) / static_cast<float>(atlas.width),
            (static_cast<float>(atlas.height) - uv_inset) / static_cast<float>(atlas.height),
        };
    }
}

#if HYPERCRAFT_USE_LIBPNG
bool read_png_rgba(std::string_view path, int expected_width, int expected_height, std::vector<std::uint8_t>& rgba)
{
    FILE* file = std::fopen(std::string(path).c_str(), "rb");
    if (file == nullptr) {
        return false;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (png == nullptr) {
        std::fclose(file);
        return false;
    }
    png_infop info = png_create_info_struct(png);
    if (info == nullptr) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        std::fclose(file);
        return false;
    }
    if (setjmp(png_jmpbuf(png)) != 0) {
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(file);
        return false;
    }

    png_init_io(png, file);
    png_read_info(png, info);
    const png_uint_32 width = png_get_image_width(png, info);
    const png_uint_32 height = png_get_image_height(png, info);
    if (width != static_cast<png_uint_32>(expected_width) || height != static_cast<png_uint_32>(expected_height)) {
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(file);
        return false;
    }

    const png_byte color_type = png_get_color_type(png, info);
    const png_byte bit_depth = png_get_bit_depth(png, info);
    if (bit_depth == 16) {
        png_set_strip_16(png);
    }
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png);
    }
    if (png_get_valid(png, info, PNG_INFO_tRNS) != 0) {
        png_set_tRNS_to_alpha(png);
    }
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }

    png_read_update_info(png, info);
    rgba.assign(static_cast<std::size_t>(expected_width * expected_height * 4), 0);
    std::vector<png_bytep> rows(static_cast<std::size_t>(expected_height));
    for (int y = 0; y < expected_height; ++y) {
        rows[static_cast<std::size_t>(y)] = rgba.data() + static_cast<std::size_t>(y * expected_width * 4);
    }
    png_read_image(png, rows.data());
    png_read_end(png, nullptr);
    png_destroy_read_struct(&png, &info, nullptr);
    std::fclose(file);
    return true;
}
#endif

void set_pixel(TextureAtlas& atlas, int x, int y, Rgba pixel)
{
    if (x < 0 || y < 0 || x >= atlas.width || y >= atlas.height) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>((y * atlas.width + x) * 4);
    atlas.rgba[index] = pixel.r;
    atlas.rgba[index + 1] = pixel.g;
    atlas.rgba[index + 2] = pixel.b;
    atlas.rgba[index + 3] = pixel.a;
}

Rgba grass_pixel(int x, int y)
{
    const double n = smooth_noise(x, y, 11, 13);
    const double tuft = noise(x, y, 12) > 0.82 ? 10.0 : 0.0;
    const double dark = noise(x, y, 13) > 0.90 ? -9.0 : 0.0;
    return rgba(53 + n + tuft + dark, 130 + n + tuft + dark, 43 + n / 2.0 + dark);
}

Rgba orange_grass_pixel(int x, int y)
{
    const double n = smooth_noise(x, y, 14, 12);
    const double tuft = noise(x, y, 15) > 0.82 ? 9.0 : 0.0;
    const double dark = noise(x, y, 16) > 0.90 ? -8.0 : 0.0;
    return rgba(156 + n + tuft + dark, 92 + n * 0.55 + tuft * 0.35 + dark, 32 + n * 0.25 + dark * 0.6);
}

Rgba dirt_pixel(int x, int y)
{
    const double n = smooth_noise(x, y, 21, 13);
    const double pebble = noise(x, y, 22) > 0.86 ? 12.0 : 0.0;
    const double root = noise(x, y, 23) > 0.93 ? -10.0 : 0.0;
    return rgba(101 + n + pebble + root, 70 + n * 0.7 + pebble * 0.5 + root, 40 + n * 0.45 + root * 0.6);
}

Rgba sand_pixel(int x, int y)
{
    const double n = smooth_noise(x, y, 24, 11);
    const double grain = noise(x, y, 25) > 0.82 ? 9.0 : 0.0;
    const double fleck = noise(x + 4, y - 3, 26) > 0.90 ? -8.0 : 0.0;
    return rgba(194 + n + grain + fleck, 172 + n * 0.75 + grain * 0.5 + fleck, 103 + n * 0.45 + fleck * 0.5);
}

Rgba snow_pixel(int x, int y)
{
    const double n = smooth_noise(x / 2, y / 2, 27, 2.5);
    const double shade = (static_cast<double>(y) / static_cast<double>(texture_tile_size - 1)) * -2.0;
    return rgba(232 + n + shade, 239 + n + shade, 242 + n * 0.8 + shade);
}

Rgba stone_pixel(int x, int y)
{
    const double n = smooth_noise(x / 2, y / 2, 31, 10);
    const double fleck = noise(x, y, 32) > 0.93 ? 7.0 : 0.0;
    const double crack = (noise(x, y, 33) > 0.975 && noise(x + 1, y, 33) > 0.62) ? -8.0 : 0.0;
    const double grain = noise(x / 3, y / 2, 34) > 0.86 ? -4.0 : 0.0;
    const double chip = noise(x, y / 2, 35) > 0.95 ? 5.0 : 0.0;
    const double speckle = std::floor(noise(x, y, 36) * 5.0) - 2.0;
    const double tint = std::floor(noise(y, x, 37) * 3.0) - 1.0;
    const double base = n + fleck + crack + grain + chip + speckle;
    return rgba(105 + base + tint, 106 + base, 111 + base - tint);
}

Rgba wood_pixel(int x, int y)
{
    const double vertical_wave = std::sin((static_cast<double>(x) + signed_noise(y / 3, 0, 44, 1.5)) * 0.9) * 5.0;
    const double grain = smooth_noise(x / 2, y, 41, 7) + vertical_wave;
    const double stripe = x == 2 || x == 7 || x == 13 ? -9.0 : 0.0;
    const double dark_line = noise(x / 2, y, 42) > 0.82 && noise(x + y, y, 43) > 0.48 ? -8.0 : 0.0;
    const double knot = (x - 10) * (x - 10) + (y - 6) * (y - 6) <= 10 ? 9.0 : 0.0;
    return rgba(
        105 + grain + knot + dark_line + stripe,
        63 + grain * 0.6 + knot * 0.5 + dark_line + stripe,
        28 + grain * 0.35 + dark_line * 0.5 + stripe * 0.5);
}

Rgba pine_wood_pixel(int x, int y)
{
    const Rgba base = wood_pixel(x, y);
    const double n = smooth_noise(x, y, 45, 4);
    return rgba(
        static_cast<double>(base.r) * 0.62 + n - 6.0,
        static_cast<double>(base.g) * 0.72 + n * 0.65 - 5.0,
        static_cast<double>(base.b) * 0.58 + n * 0.35 - 3.0);
}

Rgba jungle_wood_pixel(int x, int y)
{
    const Rgba base = wood_pixel(x, y);
    const double warm = smooth_noise(x, y, 46, 5);
    const double stripe = x == 2 || x == 7 || x == 13 ? -16.0 : 0.0;
    const double dark_line = noise(x / 2, y, 47) > 0.82 && noise(x + y, y, 48) > 0.48 ? -10.0 : 0.0;
    return rgba(
        92.0 + static_cast<double>(base.r) * 0.30 + warm + stripe + dark_line,
        66.0 + static_cast<double>(base.g) * 0.34 + warm * 0.55 + stripe * 0.65 + dark_line,
        34.0 + static_cast<double>(base.b) * 0.28 + warm * 0.35 + stripe * 0.40 + dark_line * 0.5);
}

Rgba windswept_wood_pixel(int x, int y)
{
    const Rgba base = wood_pixel(x, y);
    const double warm = smooth_noise(x, y, 49, 4);
    const double stripe = x == 3 || x == 9 || x == 14 ? -10.0 : 0.0;
    return rgba(
        static_cast<double>(base.r) * 0.95 + 18.0 + warm + stripe,
        static_cast<double>(base.g) * 0.82 + 5.0 + warm * 0.45 + stripe,
        static_cast<double>(base.b) * 0.78 + warm * 0.25 + stripe * 0.45);
}

Rgba leaves_pixel(int x, int y)
{
    const double n = smooth_noise(x, y, 51, 14);
    const double highlight = noise(x, y, 52) > 0.84 ? 10.0 : 0.0;
    const double shadow = noise(x, y, 53) > 0.90 ? -13.0 : 0.0;
    return rgba(31 + n * 0.5 + highlight + shadow, 113 + n + highlight + shadow, 37 + n * 0.5 + shadow);
}

Rgba water_pixel(int x, int y)
{
    const double n = smooth_noise(x, y, 61, 8);
    const double glint = noise(x, y, 62) > 0.94 ? 10.0 : 0.0;
    const double ripple = (static_cast<int>(std::floor(static_cast<double>(x + y) / 3.0)) % 2 == 0 ? 4.0 : -3.0)
        + signed_noise(x, y / 2, 63, 3);
    return rgba(34 + n + glint + ripple, 103 + n + glint + ripple, 210 + n * 0.6 + glint + ripple * 0.5, 180);
}

Rgba lava_pixel(int x, int y)
{
    const double n = smooth_noise(x, y, 66, 13);
    const double hot = noise(x, y, 67) > 0.78 ? 34.0 : 0.0;
    const double dark = noise(x + 2, y - 3, 68) > 0.88 ? -22.0 : 0.0;
    const bool vein = (x + y + static_cast<int>(noise(x, y, 69) * 4.0)) % 5 == 0;
    return rgba(224 + n + hot + dark, 72 + n * 0.45 + hot * 0.65 + dark, 14 + n * 0.15 + hot * 0.18, vein ? 238 : 232);
}

Rgba ice_pixel(int x, int y)
{
    const double n = smooth_noise(x, y, 64, 7);
    const double glint = noise(x, y, 65) > 0.90 ? 22.0 : 0.0;
    const bool crack = (x == y / 2 + 3 && y > 3 && y < 13) || (x + y == 18 && x > 5 && x < 13);
    return rgba(112 + n * 0.6 + glint + (crack ? -18.0 : 0.0), 186 + n + glint + (crack ? -12.0 : 0.0), 246 + n * 0.5 + glint, 180);
}

Rgba glass_pixel(int x, int y)
{
    const bool edge = x == 0 || y == 0 || x == texture_tile_size - 1 || y == texture_tile_size - 1;
    const bool highlight =
        (x == 3 && y >= 3 && y <= 5) ||
        (y == 3 && x >= 3 && x <= 5) ||
        (x == 11 && y == 10) ||
        (x == 12 && y == 11);
    if (edge) {
        return rgba(255, 255, 255, 235);
    }
    if (highlight) {
        return rgba(255, 255, 255, 180);
    }
    return rgba(255, 255, 255, 0);
}

Rgba crafting_table_pixel(int x, int y)
{
    const bool edge = x == 0 || y == 0 || x == texture_tile_size - 1 || y == texture_tile_size - 1;
    const bool grid = x == 7 || x == 8 || y == 7 || y == 8;
    const double n = smooth_noise(x, y, 71, 9);
    if (edge) {
        return rgba(94 + n, 57 + n * 0.5, 29 + n * 0.25);
    }
    if (grid) {
        return rgba(112 + n, 68 + n * 0.5, 34 + n * 0.25);
    }
    return rgba(132 + n, 84 + n * 0.5, 42 + n * 0.25);
}

Rgba planks_pixel(int x, int y)
{
    const double n = smooth_noise(x, y, 81, 10);
    const double seam = y == 5 || y == 10 ? -16.0 : 0.0;
    const double grain = noise(x, y, 82) > 0.78 ? 8.0 : 0.0;
    return rgba(142 + n + grain + seam, 92 + n * 0.55 + grain * 0.5 + seam, 45 + n * 0.3 + seam * 0.5);
}

Rgba pine_planks_pixel(int x, int y)
{
    const Rgba base = planks_pixel(x, y);
    const double n = smooth_noise(x, y, 83, 3);
    return rgba(
        static_cast<double>(base.r) * 0.68 + n - 5.0,
        static_cast<double>(base.g) * 0.78 + n * 0.65 - 4.0,
        static_cast<double>(base.b) * 0.64 + n * 0.35 - 2.0);
}

Rgba jungle_planks_pixel(int x, int y)
{
    const Rgba base = planks_pixel(x, y);
    const double warm = smooth_noise(x, y, 84, 4);
    const double seam = y == 5 || y == 10 || y == 6 || y == 11 ? -14.0 : 0.0;
    const double grain = noise(x, y, 85) > 0.78 ? 6.0 : 0.0;
    return rgba(
        108.0 + static_cast<double>(base.r) * 0.28 + warm + seam + grain,
        77.0 + static_cast<double>(base.g) * 0.32 + warm * 0.55 + seam * 0.70 + grain * 0.45,
        38.0 + static_cast<double>(base.b) * 0.25 + warm * 0.35 + seam * 0.45);
}

Rgba windswept_planks_pixel(int x, int y)
{
    const Rgba base = planks_pixel(x, y);
    const double warm = smooth_noise(x, y, 87, 4);
    const double seam = y == 5 || y == 10 ? -15.0 : 0.0;
    const double grain = noise(x, y, 88) > 0.80 ? 7.0 : 0.0;
    return rgba(
        static_cast<double>(base.r) * 1.05 + 12.0 + warm + grain + seam,
        static_cast<double>(base.g) * 0.86 + warm * 0.45 + grain * 0.35 + seam,
        static_cast<double>(base.b) * 0.82 + warm * 0.25 + seam * 0.5);
}

Rgba chest_pixel(int x, int y)
{
    const bool edge = x == 0 || y == 0 || x == texture_tile_size - 1 || y == texture_tile_size - 1;
    const bool band = x == 2 || x == 13 || y == 6;
    const bool latch = x >= 7 && x <= 9 && y >= 6 && y <= 10;
    const double seam = y == 8 ? -18.0 : 0.0;
    const double n = smooth_noise(x, y, 86, 8);
    if (latch) {
        return rgba(190 + n, 154 + n * 0.6, 64 + n * 0.25);
    }
    if (edge || band) {
        return rgba(82 + n, 51 + n * 0.5, 24 + n * 0.25);
    }
    return rgba(133 + n + seam, 82 + n * 0.55 + seam, 38 + n * 0.3 + seam * 0.4);
}

Rgba furnace_pixel(int x, int y)
{
    const bool edge = x == 0 || y == 0 || x == texture_tile_size - 1 || y == texture_tile_size - 1;
    const bool mouth = x >= 4 && x <= 11 && y >= 5 && y <= 10;
    const bool glow = x >= 6 && x <= 9 && y >= 8 && y <= 11;
    const double n = smooth_noise(x, y, 88, 11) + signed_noise(x, y, 90, 3);
    const double chip = noise(x, y, 91) > 0.88 ? 9.0 : 0.0;
    const double soot = noise(x + 5, y, 92) > 0.9 ? -10.0 : 0.0;
    if (glow && noise(x, y, 89) > 0.35) {
        return rgba(214 + n + chip, 92 + n * 0.4 + chip * 0.4, 28 + n * 0.2);
    }
    if (mouth) {
        return rgba(42 + n * 0.4 + soot, 43 + n * 0.4 + soot, 47 + n * 0.4 + soot);
    }
    if (edge) {
        return rgba(72 + n + chip, 73 + n + chip, 78 + n + chip);
    }
    return rgba(102 + n + chip + soot, 103 + n + chip + soot, 108 + n + chip + soot);
}

Rgba sticks_pixel(int x, int y)
{
    const auto distance_to_segment = [](double px, double py, double x0, double y0, double x1, double y1) {
        const double dx = x1 - x0;
        const double dy = y1 - y0;
        const double length_squared = dx * dx + dy * dy;
        const double t = std::clamp(((px - x0) * dx + (py - y0) * dy) / length_squared, 0.0, 1.0);
        const double nearest_x = x0 + t * dx;
        const double nearest_y = y0 + t * dy;
        const double out_x = px - nearest_x;
        const double out_y = py - nearest_y;
        return std::sqrt(out_x * out_x + out_y * out_y);
    };
    const double px = static_cast<double>(x) + 0.5;
    const double py = static_cast<double>(y) + 0.5;
    if (distance_to_segment(px, py, 4.0, 10.0, 10.0, 4.0) <= 1.0) {
        return rgba(184, 121, 53);
    }
    if (distance_to_segment(px, py, 4.0, 12.0, 12.0, 4.0) <= 2.5) {
        return rgba(79, 45, 19);
    }
    return rgba(0, 0, 0, 0);
}

Rgba ore_pixel(int x, int y, Rgba color, int seed)
{
    Rgba base = stone_pixel(x, y);
    const std::array<std::array<int, 3>, 7> spots {{
        {4, 4, 4},
        {11, 5, 3},
        {7, 8, 5},
        {3, 11, 3},
        {12, 12, 4},
        {9, 2, 2},
        {2, 7, 2},
    }};
    double ore_amount = 0.0;
    for (const auto spot : spots) {
        const int cx = spot[0] + static_cast<int>(signed_noise(seed, spot[1], seed + spot[2], 1.5));
        const int cy = spot[1] + static_cast<int>(signed_noise(seed + spot[0], spot[2], seed, 1.5));
        const int radius = spot[2];
        const int dx = x - cx;
        const int dy = y - cy;
        if (dx * dx + dy * dy <= radius * radius) {
            ore_amount = std::max(ore_amount, 1.0 - static_cast<double>(dx * dx + dy * dy) / static_cast<double>(radius * radius + 1));
        }
    }
    if (ore_amount <= 0.0) {
        return base;
    }
    const double sparkle = noise(x, y, seed + 91) > 0.82 ? 20.0 : 0.0;
    return rgba(
        static_cast<double>(base.r) * (1.0 - ore_amount) + (static_cast<double>(color.r) + sparkle) * ore_amount,
        static_cast<double>(base.g) * (1.0 - ore_amount) + (static_cast<double>(color.g) + sparkle) * ore_amount,
        static_cast<double>(base.b) * (1.0 - ore_amount) + (static_cast<double>(color.b) + sparkle) * ore_amount);
}

Rgba blend_over(Rgba destination, Rgba source)
{
    const double source_alpha = static_cast<double>(source.a) / 255.0;
    const double destination_alpha = static_cast<double>(destination.a) / 255.0;
    const double output_alpha = source_alpha + destination_alpha * (1.0 - source_alpha);
    if (output_alpha <= 0.0) {
        return {};
    }
    const auto blend_channel = [source_alpha, destination_alpha, output_alpha](std::uint8_t source_value, std::uint8_t destination_value) {
        return (static_cast<double>(source_value) * source_alpha +
                   static_cast<double>(destination_value) * destination_alpha * (1.0 - source_alpha)) /
            output_alpha;
    };
    return rgba(
        blend_channel(source.r, destination.r),
        blend_channel(source.g, destination.g),
        blend_channel(source.b, destination.b),
        output_alpha * 255.0);
}

Rgba item_tool_pixel(int x, int y, Rgba base)
{
    Rgba pixel = rgba(0, 0, 0, 0);
    if (x >= 2 && x < 14 && y >= 2 && y < 14) {
        pixel = base;
    }
    if (x >= 4 && x < 8 && y >= 4 && y < 6) {
        pixel = blend_over(pixel, rgba(255, 255, 255, 140));
    }
    if ((x >= 7 && x < 10 && y >= 2 && y < 14) || (x >= 2 && x < 14 && y >= 5 && y < 9)) {
        pixel = blend_over(pixel, rgba(0, 0, 0, 97));
    }
    return pixel;
}

Rgba item_ingot_pixel(int x, int y, Rgba base)
{
    Rgba pixel = rgba(0, 0, 0, 0);
    const bool body =
        (y >= 5 && y <= 10 && x >= 3 && x <= 13) ||
        (y == 4 && x >= 5 && x <= 11) ||
        (y == 11 && x >= 4 && x <= 12);
    if (!body) {
        return pixel;
    }

    pixel = base;
    const bool edge = y == 4 || y == 11 || x == 3 || x == 13 || (y == 5 && (x <= 4 || x >= 12));
    const bool highlight = y <= 6 && x >= 5 && x <= 10;
    const bool shadow = y >= 9 || x >= 12;
    if (edge || shadow) {
        pixel = blend_over(pixel, rgba(0, 0, 0, shadow ? 85 : 55));
    }
    if (highlight) {
        pixel = blend_over(pixel, rgba(255, 255, 255, 115));
    }
    return pixel;
}

Rgba bucket_pixel(int x, int y, Rgba fill)
{
    Rgba pixel = rgba(0, 0, 0, 0);
    const bool shell =
        (y >= 4 && y <= 13 && x >= 4 && x <= 11) ||
        (y == 3 && x >= 3 && x <= 12) ||
        (y == 14 && x >= 5 && x <= 10);
    if (!shell) {
        return pixel;
    }

    const bool interior = x >= 5 && x <= 10 && y >= 6 && y <= 11;
    if (fill.a > 0 && interior) {
        pixel = fill;
    } else {
        pixel = rgba(178, 185, 190);
    }
    const bool edge = x == 4 || x == 11 || y == 3 || y == 13 || (y == 14 && (x == 5 || x == 10));
    if (edge) {
        pixel = blend_over(pixel, rgba(0, 0, 0, 95));
    }
    if (x >= 5 && x <= 8 && y >= 4 && y <= 5) {
        pixel = blend_over(pixel, rgba(255, 255, 255, 100));
    }
    return pixel;
}

Rgba torch_pixel(int x, int y)
{
    if (y >= 7 && y <= 15 && x >= 7 && x <= 8) {
        return x == 7 ? rgba(117, 70, 33) : rgba(163, 101, 43);
    }
    if (y >= 2 && y <= 7 && x >= 5 && x <= 10) {
        if (y <= 3 && x >= 7 && x <= 8) {
            return rgba(255, 245, 126);
        }
        if (x >= 6 && x <= 9) {
            return rgba(255, 174, 38);
        }
        return rgba(221, 76, 20);
    }
    return rgba(0, 0, 0, 0);
}

Rgba pixel_for_block(BlockId block, int x, int y)
{
    switch (block) {
    case BlockId::grass:
        return grass_pixel(x, y);
    case BlockId::orange_grass:
        return orange_grass_pixel(x, y);
    case BlockId::dirt:
        return dirt_pixel(x, y);
    case BlockId::sand:
        return sand_pixel(x, y);
    case BlockId::snow:
        return snow_pixel(x, y);
    case BlockId::stone:
        return stone_pixel(x, y);
    case BlockId::wood:
        return wood_pixel(x, y);
    case BlockId::pine_wood:
        return pine_wood_pixel(x, y);
    case BlockId::jungle_wood:
        return jungle_wood_pixel(x, y);
    case BlockId::windswept_wood:
        return windswept_wood_pixel(x, y);
    case BlockId::leaves:
        return leaves_pixel(x, y);
    case BlockId::water:
        return water_pixel(x, y);
    case BlockId::lava:
        return lava_pixel(x, y);
    case BlockId::ice:
        return ice_pixel(x, y);
    case BlockId::glass:
        return glass_pixel(x, y);
    case BlockId::crafting_table:
        return crafting_table_pixel(x, y);
    case BlockId::planks:
        return planks_pixel(x, y);
    case BlockId::pine_planks:
        return pine_planks_pixel(x, y);
    case BlockId::jungle_planks:
        return jungle_planks_pixel(x, y);
    case BlockId::windswept_planks:
        return windswept_planks_pixel(x, y);
    case BlockId::sticks:
        return sticks_pixel(x, y);
    case BlockId::tin_ore:
        return ore_pixel(x, y, rgba(210, 228, 236), 91);
    case BlockId::copper_ore:
        return ore_pixel(x, y, rgba(220, 112, 36), 101);
    case BlockId::iron_ore:
        return ore_pixel(x, y, rgba(222, 78, 58), 111);
    case BlockId::coal_ore:
        return ore_pixel(x, y, rgba(34, 35, 39), 121);
    case BlockId::chest:
        return chest_pixel(x, y);
    case BlockId::furnace:
        return furnace_pixel(x, y);
    case BlockId::tin_bar:
        return item_ingot_pixel(x, y, rgba(208, 216, 222));
    case BlockId::copper_bar:
        return item_ingot_pixel(x, y, rgba(209, 125, 66));
    case BlockId::iron_bar:
        return item_ingot_pixel(x, y, rgba(213, 139, 114));
    case BlockId::bucket:
        return bucket_pixel(x, y, rgba(0, 0, 0, 0));
    case BlockId::water_bucket:
        return bucket_pixel(x, y, rgba(35, 105, 210, 230));
    case BlockId::lava_bucket:
        return bucket_pixel(x, y, rgba(226, 77, 17, 238));
    case BlockId::wood_tool:
        return item_tool_pixel(x, y, rgba(183, 122, 52));
    case BlockId::stone_tool:
        return item_tool_pixel(x, y, rgba(154, 157, 163));
    case BlockId::tin_tool:
        return item_tool_pixel(x, y, rgba(184, 189, 181));
    case BlockId::copper_tool:
        return item_tool_pixel(x, y, rgba(192, 109, 54));
    case BlockId::iron_tool:
        return item_tool_pixel(x, y, rgba(181, 134, 97));
    case BlockId::torch:
        return torch_pixel(x, y);
    case BlockId::air:
        break;
    }
    return rgba(255, 0, 255);
}

std::string_view block_texture_file_stem(BlockId block)
{
    const ContentDefinition& definition = ContentRegistry::default_registry().definition(block);
    return definition.texture.empty() ? definition.name : std::string_view(definition.texture);
}

void write_tile(TextureAtlas& atlas, int tile, const std::vector<std::uint8_t>& tile_rgba)
{
    for (int y = 0; y < texture_tile_size; ++y) {
        for (int x = 0; x < texture_tile_size; ++x) {
            const std::size_t source = static_cast<std::size_t>((y * texture_tile_size + x) * 4);
            const int atlas_x = tile * texture_tile_size + x;
            const std::size_t destination = static_cast<std::size_t>((y * atlas.width + atlas_x) * 4);
            atlas.rgba[destination] = tile_rgba[source];
            atlas.rgba[destination + 1] = tile_rgba[source + 1];
            atlas.rgba[destination + 2] = tile_rgba[source + 2];
            atlas.rgba[destination + 3] = tile_rgba[source + 3];
        }
    }
}

void write_procedural_block_tile(TextureAtlas& atlas, int tile, BlockId block)
{
    for (int y = 0; y < texture_tile_size; ++y) {
        for (int x = 0; x < texture_tile_size; ++x) {
            const Rgba pixel = pixel_for_block(block, x, y);
            const int atlas_x = tile * texture_tile_size + x;
            const std::size_t index = static_cast<std::size_t>((y * atlas.width + atlas_x) * 4);
            atlas.rgba[index] = pixel.r;
            atlas.rgba[index + 1] = pixel.g;
            atlas.rgba[index + 2] = pixel.b;
            atlas.rgba[index + 3] = pixel.a;
        }
    }
}

#if HYPERCRAFT_USE_LIBPNG
bool load_named_tile(TextureAtlas& atlas, int tile, std::string_view directory, std::string_view stem)
{
    std::vector<std::uint8_t> tile_rgba;
    const std::string path = std::string(directory) + "/" + std::string(stem) + ".png";
    if (!read_png_rgba(path, texture_tile_size, texture_tile_size, tile_rgba)) {
        return false;
    }
    write_tile(atlas, tile, tile_rgba);
    return true;
}

bool load_block_tile(TextureAtlas& atlas, int tile, BlockId block)
{
    #ifdef HYPERCRAFT_ASSET_DIR
    if (load_named_tile(atlas, tile, std::string(HYPERCRAFT_ASSET_DIR) + "/textures/blocks", block_texture_file_stem(block))) {
        return true;
    }
    #endif
    return load_named_tile(atlas, tile, "assets/textures/blocks", block_texture_file_stem(block)) ||
        load_named_tile(atlas, tile, "/assets/textures/blocks", block_texture_file_stem(block));
}

bool load_crack_tile(TextureAtlas& atlas, int tile, int stage)
{
    const std::string stem = "crack_" + std::to_string(stage);
    #ifdef HYPERCRAFT_ASSET_DIR
    if (load_named_tile(atlas, tile, std::string(HYPERCRAFT_ASSET_DIR) + "/textures/cracks", stem)) {
        return true;
    }
    #endif
    return load_named_tile(atlas, tile, "assets/textures/cracks", stem) ||
        load_named_tile(atlas, tile, "/assets/textures/cracks", stem);
}
#endif

struct CrackLine {
    int x0;
    int y0;
    int x1;
    int y1;
    Rgba color;
};

void draw_crack_line(TextureAtlas& atlas, int tile_x, CrackLine line)
{
    const int dx = std::abs(line.x1 - line.x0);
    const int dy = -std::abs(line.y1 - line.y0);
    const int step_x = line.x0 < line.x1 ? 1 : -1;
    const int step_y = line.y0 < line.y1 ? 1 : -1;
    int error = dx + dy;
    int x = line.x0;
    int y = line.y0;
    while (true) {
        set_pixel(atlas, tile_x + x, y, line.color);
        if (x == line.x1 && y == line.y1) {
            break;
        }
        const int doubled = error * 2;
        if (doubled >= dy) {
            error += dy;
            x += step_x;
        }
        if (doubled <= dx) {
            error += dx;
            y += step_y;
        }
    }
}

} // namespace

TextureAtlas create_block_texture_atlas()
{
    const std::vector<BlockId> blocks = block_texture_order();
    TextureAtlas atlas;
    atlas.width = texture_tile_size * static_cast<int>(blocks.size() + crack_stage_count);
    atlas.height = atlas_height;
    atlas.blocks.resize(max_content_definition_count);
    atlas.rgba.assign(static_cast<std::size_t>(atlas.width * atlas.height * 4), 0);
    assign_texture_regions(atlas);

    for (std::size_t tile = 0; tile < blocks.size(); ++tile) {
        const BlockId block = blocks[tile];
        #if HYPERCRAFT_USE_LIBPNG
        if (load_block_tile(atlas, static_cast<int>(tile), block)) {
            continue;
        }
        #endif
        write_procedural_block_tile(atlas, static_cast<int>(tile), block);
    }

    const Rgba black = rgba(15, 12, 10, 190);
    const Rgba dark = rgba(45, 35, 28, 140);
    const Rgba faint = rgba(58, 47, 39, 105);
    const std::array<std::vector<CrackLine>, crack_stage_count> cracks {{
        {{{7, 8, 8, 7, black}, {8, 7, 9, 8, faint}}},
        {{{7, 8, 8, 7, black}, {8, 7, 10, 6, dark}, {7, 8, 5, 7, faint}}},
        {{{7, 8, 8, 7, black}, {8, 7, 10, 6, dark}, {7, 8, 5, 7, dark}, {8, 8, 9, 10, faint}}},
        {{{7, 8, 8, 7, black}, {8, 7, 10, 6, dark}, {10, 6, 11, 4, faint}, {7, 8, 5, 7, dark}, {5, 7, 4, 5, faint}, {8, 8, 9, 10, dark}}},
        {{{7, 8, 8, 7, black}, {8, 7, 10, 6, black}, {10, 6, 12, 4, dark}, {7, 8, 5, 7, black}, {5, 7, 3, 5, dark}, {8, 8, 9, 10, dark}, {9, 10, 8, 12, faint}}},
        {{{7, 8, 8, 7, black}, {8, 7, 10, 6, black}, {10, 6, 12, 4, dark}, {10, 6, 13, 7, faint}, {7, 8, 5, 7, black}, {5, 7, 3, 5, dark}, {5, 7, 4, 9, faint}, {8, 8, 9, 10, dark}, {9, 10, 8, 12, dark}}},
        {{{7, 8, 8, 7, black}, {8, 7, 10, 6, black}, {10, 6, 13, 4, dark}, {10, 6, 13, 7, dark}, {7, 8, 5, 7, black}, {5, 7, 2, 5, dark}, {5, 7, 4, 10, dark}, {8, 8, 9, 10, black}, {9, 10, 8, 13, dark}, {8, 8, 6, 10, faint}}},
        {{{7, 8, 8, 7, black}, {8, 7, 10, 6, black}, {10, 6, 13, 4, black}, {10, 6, 14, 7, dark}, {13, 4, 14, 2, faint}, {7, 8, 5, 7, black}, {5, 7, 2, 5, black}, {5, 7, 4, 11, dark}, {2, 5, 1, 3, faint}, {8, 8, 9, 10, black}, {9, 10, 8, 14, dark}, {8, 8, 6, 10, dark}, {6, 10, 4, 12, faint}}},
    }};
    for (int stage = 0; stage < crack_stage_count; ++stage) {
        const int tile = static_cast<int>(blocks.size()) + stage;
        #if HYPERCRAFT_USE_LIBPNG
        if (load_crack_tile(atlas, tile, stage)) {
            continue;
        }
        #endif
        for (const CrackLine& line : cracks[static_cast<std::size_t>(stage)]) {
            draw_crack_line(atlas, tile * texture_tile_size, line);
        }
    }
    return atlas;
}

std::string_view texture_file_stem_for_block(BlockId block)
{
    return block_texture_file_stem(block);
}

TextureRegion texture_region_for_block(const TextureAtlas& atlas, BlockId block)
{
    const std::size_t index = static_cast<std::size_t>(block == BlockId::air ? BlockId::grass : block);
    if (index >= atlas.blocks.size()) {
        return atlas.blocks[static_cast<std::size_t>(BlockId::grass)];
    }
    return atlas.blocks[index];
}

bool texture_has_alpha(BlockId block)
{
    const ContentDefinition& definition = ContentRegistry::default_registry().definition(block);
    return definition.alpha_texture || (block != BlockId::air && !definition.renderable);
}

TextureRegion texture_region_for_crack_stage(const TextureAtlas& atlas, int stage)
{
    stage = std::clamp(stage, 0, static_cast<int>(atlas.cracks.size()) - 1);
    return atlas.cracks[static_cast<std::size_t>(stage)];
}

} // namespace hypercraft
