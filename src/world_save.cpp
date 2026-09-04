#include "proj4d/world_save.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <optional>
#include <system_error>
#include <vector>

namespace proj4d {

namespace {

constexpr std::array<std::uint8_t, 8> saveMagic{
    'P', 'R', 'O', 'J', '4', 'D', 'W', 'S',
};
constexpr std::uint32_t saveFormatVersion = 1U;
constexpr std::uint64_t fnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t fnvPrime = 1099511628211ULL;
constexpr std::uintmax_t maximumSaveBytes = 512ULL * 1024ULL * 1024ULL;
constexpr std::uintmax_t fixedSaveBytes = 36U;
constexpr std::uintmax_t bytesPerEdit = 17U;

void hashByte(std::uint64_t &hash, std::uint8_t value) {
  hash ^= value;
  hash *= fnvPrime;
}

bool writeByte(std::ostream &output, std::uint8_t value, std::uint64_t *hash) {
  output.put(static_cast<char>(value));
  if (hash != nullptr) {
    hashByte(*hash, value);
  }
  return static_cast<bool>(output);
}

bool writeUnsigned(std::ostream &output, std::uint64_t value,
                   std::size_t byteCount, std::uint64_t *hash) {
  for (std::size_t byte = 0; byte < byteCount; ++byte) {
    if (!writeByte(output, static_cast<std::uint8_t>(value & 0xFFU), hash)) {
      return false;
    }
    value >>= 8U;
  }
  return true;
}

bool readByte(std::istream &input, std::uint8_t &value, std::uint64_t *hash) {
  const int byte = input.get();
  if (byte == std::char_traits<char>::eof()) {
    return false;
  }
  value = static_cast<std::uint8_t>(byte);
  if (hash != nullptr) {
    hashByte(*hash, value);
  }
  return true;
}

bool readUnsigned(std::istream &input, std::uint64_t &value,
                  std::size_t byteCount, std::uint64_t *hash) {
  value = 0U;
  for (std::size_t byte = 0; byte < byteCount; ++byte) {
    std::uint8_t current{};
    if (!readByte(input, current, hash)) {
      return false;
    }
    value |= static_cast<std::uint64_t>(current) << (byte * 8U);
  }
  return true;
}

std::uint32_t terrainCode(TerrainMode mode) {
  switch (mode) {
  case TerrainMode::Flat:
    return 0U;
  case TerrainMode::Low:
    return 1U;
  case TerrainMode::Density:
    return 2U;
  }
  return 0U;
}

bool replaceSaveFile(const std::filesystem::path &temporaryPath,
                     const std::filesystem::path &savePath,
                     std::string &error) {
  std::error_code renameError;
  std::filesystem::rename(temporaryPath, savePath, renameError);
  if (!renameError) {
    return true;
  }

  std::error_code existsError;
  if (!std::filesystem::exists(savePath, existsError) || existsError) {
    error =
        "could not install the completed save file: " + renameError.message();
    std::error_code ignored;
    std::filesystem::remove(temporaryPath, ignored);
    return false;
  }

  std::filesystem::path backupPath = savePath;
  backupPath += ".backup";
  std::error_code ignored;
  std::filesystem::remove(backupPath, ignored);
  std::error_code backupError;
  std::filesystem::rename(savePath, backupPath, backupError);
  if (backupError) {
    error =
        "could not preserve the previous save file: " + backupError.message();
    std::filesystem::remove(temporaryPath, ignored);
    return false;
  }

  std::error_code installError;
  std::filesystem::rename(temporaryPath, savePath, installError);
  if (installError) {
    std::error_code restoreError;
    std::filesystem::rename(backupPath, savePath, restoreError);
    error =
        "could not replace the previous save file: " + installError.message();
    std::filesystem::remove(temporaryPath, ignored);
    return false;
  }
  std::filesystem::remove(backupPath, ignored);
  return true;
}

bool coordinateLess(const BlockEdit &left, const BlockEdit &right) {
  return left.coordinate < right.coordinate;
}

} // namespace

std::filesystem::path worldSaveFilename(TerrainMode mode) {
  switch (mode) {
  case TerrainMode::Flat:
    return "flat.p4world";
  case TerrainMode::Low:
    return "low.p4world";
  case TerrainMode::Density:
    return "high.p4world";
  }
  return "flat.p4world";
}

bool saveWorldSave(const std::filesystem::path &path, const BlockWorld &world,
                   std::string &error) {
  error.clear();
  std::error_code directoryError;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), directoryError);
  }
  if (directoryError) {
    error = "could not create the world save directory: " +
            directoryError.message();
    return false;
  }

  std::vector<BlockEdit> edits = world.edits();
  std::sort(edits.begin(), edits.end(), coordinateLess);
  const std::uintmax_t editCount = edits.size();
  if (editCount > (maximumSaveBytes - fixedSaveBytes) / bytesPerEdit) {
    error = "the world contains too many edits for this save format";
    return false;
  }

  std::filesystem::path temporaryPath = path;
  temporaryPath += ".tmp";
  std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
  if (!output) {
    error = "could not open the temporary world save";
    return false;
  }

  std::uint64_t hash = fnvOffset;
  for (const std::uint8_t byte : saveMagic) {
    static_cast<void>(writeByte(output, byte, &hash));
  }
  static_cast<void>(writeUnsigned(output, saveFormatVersion, 4U, &hash));
  static_cast<void>(
      writeUnsigned(output, terrainCode(world.terrainMode()), 4U, &hash));
  static_cast<void>(writeUnsigned(output, world.seed(), 4U, &hash));
  static_cast<void>(writeUnsigned(output, edits.size(), 8U, &hash));
  for (const BlockEdit &edit : edits) {
    static_cast<void>(writeUnsigned(
        output, static_cast<std::uint32_t>(edit.coordinate.x), 4U, &hash));
    static_cast<void>(writeUnsigned(
        output, static_cast<std::uint32_t>(edit.coordinate.y), 4U, &hash));
    static_cast<void>(writeUnsigned(
        output, static_cast<std::uint32_t>(edit.coordinate.z), 4U, &hash));
    static_cast<void>(writeUnsigned(
        output, static_cast<std::uint32_t>(edit.coordinate.w), 4U, &hash));
    static_cast<void>(writeByte(output, edit.solid ? 1U : 0U, &hash));
  }
  static_cast<void>(writeUnsigned(output, hash, 8U, nullptr));
  output.flush();
  if (!output) {
    error = "could not finish writing the world save";
    output.close();
    std::error_code ignored;
    std::filesystem::remove(temporaryPath, ignored);
    return false;
  }
  output.close();
  return replaceSaveFile(temporaryPath, path, error);
}

WorldLoadStatus loadWorldSave(const std::filesystem::path &path,
                              BlockWorld &world, std::string &error) {
  error.clear();
  std::error_code existsError;
  const bool exists = std::filesystem::exists(path, existsError);
  if (existsError) {
    error = "could not inspect the world save: " + existsError.message();
    return WorldLoadStatus::Error;
  }
  if (!exists) {
    return WorldLoadStatus::NotFound;
  }

  std::error_code sizeError;
  const std::uintmax_t fileSize = std::filesystem::file_size(path, sizeError);
  if (sizeError || fileSize < fixedSaveBytes || fileSize > maximumSaveBytes) {
    error = "the world save has an invalid size";
    return WorldLoadStatus::Error;
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "could not open the world save";
    return WorldLoadStatus::Error;
  }

  std::uint64_t hash = fnvOffset;
  for (const std::uint8_t expected : saveMagic) {
    std::uint8_t actual{};
    if (!readByte(input, actual, &hash) || actual != expected) {
      error = "the world save has an invalid signature";
      return WorldLoadStatus::Error;
    }
  }
  std::uint64_t version{};
  std::uint64_t savedTerrain{};
  std::uint64_t savedSeed{};
  std::uint64_t editCount{};
  if (!readUnsigned(input, version, 4U, &hash) ||
      !readUnsigned(input, savedTerrain, 4U, &hash) ||
      !readUnsigned(input, savedSeed, 4U, &hash) ||
      !readUnsigned(input, editCount, 8U, &hash)) {
    error = "the world save header is incomplete";
    return WorldLoadStatus::Error;
  }
  if (version != saveFormatVersion) {
    error = "the world save uses an unsupported format version";
    return WorldLoadStatus::Error;
  }
  if (savedTerrain != terrainCode(world.terrainMode()) ||
      savedSeed != world.seed()) {
    error = "the world save belongs to a different terrain world";
    return WorldLoadStatus::Error;
  }
  if (editCount > (maximumSaveBytes - fixedSaveBytes) / bytesPerEdit ||
      fileSize != fixedSaveBytes + editCount * bytesPerEdit) {
    error = "the world save edit count does not match its size";
    return WorldLoadStatus::Error;
  }

  std::vector<BlockEdit> edits;
  edits.reserve(static_cast<std::size_t>(editCount));
  std::optional<BlockCoord> previous;
  for (std::uint64_t index = 0; index < editCount; ++index) {
    std::array<std::uint64_t, 4> coordinates{};
    std::uint8_t solid{};
    for (std::uint64_t &coordinate : coordinates) {
      if (!readUnsigned(input, coordinate, 4U, &hash)) {
        error = "the world save ends inside an edit";
        return WorldLoadStatus::Error;
      }
    }
    if (!readByte(input, solid, &hash) || solid > 1U) {
      error = "the world save contains an invalid block value";
      return WorldLoadStatus::Error;
    }
    const BlockCoord coordinate{
        static_cast<int>(std::bit_cast<std::int32_t>(
            static_cast<std::uint32_t>(coordinates[0]))),
        static_cast<int>(std::bit_cast<std::int32_t>(
            static_cast<std::uint32_t>(coordinates[1]))),
        static_cast<int>(std::bit_cast<std::int32_t>(
            static_cast<std::uint32_t>(coordinates[2]))),
        static_cast<int>(std::bit_cast<std::int32_t>(
            static_cast<std::uint32_t>(coordinates[3]))),
    };
    if (previous && !(previous.value() < coordinate)) {
      error = "the world save contains duplicate or unsorted edits";
      return WorldLoadStatus::Error;
    }
    previous = coordinate;
    edits.push_back({coordinate, solid != 0U});
  }

  std::uint64_t savedHash{};
  if (!readUnsigned(input, savedHash, 8U, nullptr) || savedHash != hash) {
    error = "the world save checksum does not match";
    return WorldLoadStatus::Error;
  }
  world.replaceEdits(edits);
  return WorldLoadStatus::Loaded;
}

} // namespace proj4d
