#include <avernal/asset_compiler/compile.hpp>
#include <avernal/assets_render/image.hpp>
#include <avernal/terrain/avheightmap.hpp>

#include <cctype>
#include <fstream>
#include <vector>

namespace avernal {
namespace {

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

[[nodiscard]] std::string lowercase_ext(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    for (char& ch : ext) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return ext;
}

[[nodiscard]] std::string sanitize_stem(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (const char ch : name) {
        const auto uc = static_cast<unsigned char>(ch);
        if (std::isalnum(uc) || ch == '_' || ch == '-') {
            out.push_back(ch);
        } else if (ch == ' ' || ch == '.') {
            out.push_back('_');
        }
    }
    while (!out.empty() && (out.front() == '_')) {
        out.erase(out.begin());
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    return out.empty() ? std::string{"heightmap"} : out;
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>> read_bytes(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    if (!file) {
        return std::nullopt;
    }
    const auto size = static_cast<std::size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(size);
    if (size > 0 &&
        !file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
        return std::nullopt;
    }
    return bytes;
}

}  // namespace

std::optional<CompiledHeightmap> compile_heightmap(const std::filesystem::path& input,
    const std::filesystem::path& out_dir, std::string_view stem, const CompileHeightmapOptions& options,
    std::string* error) {
    if (input.empty()) {
        set_error(error, "missing input path");
        return std::nullopt;
    }
    if (!std::filesystem::exists(input)) {
        set_error(error, "input file not found: " + input.string());
        return std::nullopt;
    }
    if (options.max_height < options.min_height) {
        set_error(error, "max height must be greater than or equal to min height");
        return std::nullopt;
    }

    const auto ext = lowercase_ext(input);
    terrain::Heightmap heightmap{};
    heightmap.min_height = options.min_height;
    heightmap.max_height = options.max_height;

    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp") {
        const auto image = load_image_rgba8(input);
        if (!image) {
            set_error(error, "failed to decode heightmap image");
            return std::nullopt;
        }
        if (image->width < 2 || image->height < 2) {
            set_error(error, "heightmap image must be at least 2x2");
            return std::nullopt;
        }
        heightmap.width = image->width;
        heightmap.height = image->height;
        heightmap.samples.resize(static_cast<std::size_t>(image->width) * image->height);
        for (std::size_t i = 0; i < heightmap.samples.size(); ++i) {
            const auto* pixel = image->pixels.data() + i * 4;
            const float luminance = (0.299f * static_cast<float>(pixel[0]) +
                                        0.587f * static_cast<float>(pixel[1]) +
                                        0.114f * static_cast<float>(pixel[2])) /
                                    255.0f;
            heightmap.samples[i] =
                options.min_height + luminance * (options.max_height - options.min_height);
        }
    } else if (ext == ".raw" || ext == ".r16") {
        if (options.raw_width < 2 || options.raw_height < 2) {
            set_error(error, "raw heightmaps require --raw-width and --raw-height (>= 2)");
            return std::nullopt;
        }
        const auto bytes = read_bytes(input);
        if (!bytes) {
            set_error(error, "failed to read raw heightmap");
            return std::nullopt;
        }
        const auto expected = static_cast<std::size_t>(options.raw_width) * options.raw_height * 2u;
        if (bytes->size() < expected) {
            set_error(error, "raw heightmap is smaller than width * height * 2");
            return std::nullopt;
        }
        std::vector<std::uint16_t> packed(static_cast<std::size_t>(options.raw_width) * options.raw_height);
        for (std::size_t i = 0; i < packed.size(); ++i) {
            packed[i] = static_cast<std::uint16_t>((*bytes)[i * 2] | ((*bytes)[i * 2 + 1] << 8));
        }
        heightmap = terrain::heightmap_from_u16(
            options.raw_width, options.raw_height, options.min_height, options.max_height, packed);
    } else {
        set_error(error, "expected a heightmap image (.png/.jpg/.tga/.bmp) or raw (.raw/.r16) file");
        return std::nullopt;
    }

    if (!heightmap.is_valid()) {
        set_error(error, "invalid heightmap data");
        return std::nullopt;
    }

    auto file_stem = std::string{stem};
    if (file_stem.empty()) {
        file_stem = sanitize_stem(input.stem().string());
    } else {
        file_stem = sanitize_stem(file_stem);
    }

    std::filesystem::create_directories(out_dir);
    CompiledHeightmap compiled{};
    compiled.path = out_dir / (file_stem + ".avheightmap");
    if (!terrain::save_avheightmap(compiled.path, heightmap)) {
        set_error(error, "failed to write " + compiled.path.string());
        return std::nullopt;
    }
    return compiled;
}

}  // namespace avernal
