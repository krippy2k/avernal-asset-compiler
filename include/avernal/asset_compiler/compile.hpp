#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avernal {

struct CompiledGltf {
    std::filesystem::path model{};
    std::vector<std::filesystem::path> meshes{};
    std::vector<std::filesystem::path> materials{};
    std::vector<std::filesystem::path> textures{};
};

[[nodiscard]] std::optional<CompiledGltf> compile_gltf(const std::filesystem::path& input,
    const std::filesystem::path& out_dir, std::string_view stem = {}, std::string* error = nullptr);

struct CompiledHeightmap {
    std::filesystem::path path{};
};

struct CompileHeightmapOptions {
    float min_height{0.0f};
    float max_height{1.0f};
    std::uint32_t raw_width{};
    std::uint32_t raw_height{};
};

[[nodiscard]] std::optional<CompiledHeightmap> compile_heightmap(const std::filesystem::path& input,
    const std::filesystem::path& out_dir, std::string_view stem = {},
    const CompileHeightmapOptions& options = {}, std::string* error = nullptr);

}  // namespace avernal
