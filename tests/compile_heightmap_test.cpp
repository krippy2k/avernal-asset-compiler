#include <avernal/asset_compiler/compile.hpp>
#include <avernal/terrain/avheightmap.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

[[nodiscard]] bool write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream file{path, std::ios::binary};
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

}  // namespace

TEST(CompileHeightmap, CompilesRawU16) {
    const auto dir = std::filesystem::temp_directory_path() / "avernal-asset-compiler-heightmap";
    std::filesystem::remove_all(dir);
    const auto raw_path = dir / "ramp.raw";

    const std::uint16_t samples[] = {0, 32768, 65535, 0, 32768, 65535, 0, 32768, 65535};
    std::vector<std::uint8_t> bytes(sizeof(samples));
    std::memcpy(bytes.data(), samples, sizeof(samples));
    ASSERT_TRUE(write_file(raw_path, bytes));

    std::string error;
    const auto compiled = avernal::compile_heightmap(raw_path, dir / "out", "haven",
        {
            .min_height = 0.0f,
            .max_height = 180.0f,
            .raw_width = 3,
            .raw_height = 3,
        },
        &error);
    ASSERT_TRUE(compiled.has_value()) << error;
    ASSERT_TRUE(std::filesystem::exists(compiled->path));

    const auto heightmap = avernal::terrain::load_avheightmap(compiled->path);
    ASSERT_TRUE(heightmap.has_value());
    EXPECT_EQ(heightmap->width, 3u);
    EXPECT_EQ(heightmap->height, 3u);
    EXPECT_NEAR(heightmap->sample(0u, 0u), 0.0f, 0.01f);
    EXPECT_NEAR(heightmap->sample(2u, 0u), 180.0f, 0.01f);

    std::filesystem::remove_all(dir);
}
