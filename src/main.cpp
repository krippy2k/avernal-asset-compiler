#include <avernal/asset_compiler/compile.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void print_usage(std::ostream& out, const char* argv0) {
    out << "Usage:\n"
        << "  " << argv0 << " compile <input> --out-dir <dir> [--stem name]\n"
        << "      [--min-height n] [--max-height n] [--raw-width n] [--raw-height n]\n\n"
        << "Compile a glTF Binary (.glb/.gltf) into Avernal cooked assets:\n"
        << "  <stem>.avmodel, meshes/*.avmesh, materials/*.avmat, textures/*.avtex\n\n"
        << "Compile a heightmap (.png/.jpg/.tga/.bmp or 16-bit LE .raw/.r16) into:\n"
        << "  <stem>.avheightmap\n";
}

[[nodiscard]] std::string lowercase_ext(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    for (char& ch : ext) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return ext;
}

[[nodiscard]] bool parse_u32(std::string_view text, std::uint32_t& out) {
    try {
        const auto value = std::stoul(std::string{text});
        out = static_cast<std::uint32_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool parse_f32(std::string_view text, float& out) {
    try {
        out = std::stof(std::string{text});
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(std::cerr, argv[0]);
        return EXIT_FAILURE;
    }

    const std::string_view command{argv[1]};
    if (command == "-h" || command == "--help") {
        print_usage(std::cout, argv[0]);
        return EXIT_SUCCESS;
    }
    if (command != "compile") {
        std::cerr << "unknown command: " << command << "\n";
        print_usage(std::cerr, argv[0]);
        return EXIT_FAILURE;
    }

    std::filesystem::path input;
    std::filesystem::path out_dir;
    std::string stem;
    avernal::CompileHeightmapOptions heightmap_options{};
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--out-dir") {
            if (i + 1 >= argc) {
                std::cerr << "--out-dir requires a path\n";
                return EXIT_FAILURE;
            }
            out_dir = argv[++i];
        } else if (arg == "--stem") {
            if (i + 1 >= argc) {
                std::cerr << "--stem requires a name\n";
                return EXIT_FAILURE;
            }
            stem = argv[++i];
        } else if (arg == "--min-height") {
            if (i + 1 >= argc || !parse_f32(argv[i + 1], heightmap_options.min_height)) {
                std::cerr << "--min-height requires a number\n";
                return EXIT_FAILURE;
            }
            ++i;
        } else if (arg == "--max-height") {
            if (i + 1 >= argc || !parse_f32(argv[i + 1], heightmap_options.max_height)) {
                std::cerr << "--max-height requires a number\n";
                return EXIT_FAILURE;
            }
            ++i;
        } else if (arg == "--raw-width") {
            if (i + 1 >= argc || !parse_u32(argv[i + 1], heightmap_options.raw_width)) {
                std::cerr << "--raw-width requires a positive integer\n";
                return EXIT_FAILURE;
            }
            ++i;
        } else if (arg == "--raw-height") {
            if (i + 1 >= argc || !parse_u32(argv[i + 1], heightmap_options.raw_height)) {
                std::cerr << "--raw-height requires a positive integer\n";
                return EXIT_FAILURE;
            }
            ++i;
        } else if (arg == "-h" || arg == "--help") {
            print_usage(std::cout, argv[0]);
            return EXIT_SUCCESS;
        } else if (arg.starts_with("-")) {
            std::cerr << "unknown option: " << arg << "\n";
            print_usage(std::cerr, argv[0]);
            return EXIT_FAILURE;
        } else if (input.empty()) {
            input = std::string{arg};
        } else {
            std::cerr << "unexpected argument: " << arg << "\n";
            print_usage(std::cerr, argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (input.empty() || out_dir.empty()) {
        print_usage(std::cerr, argv[0]);
        return EXIT_FAILURE;
    }

    const auto ext = lowercase_ext(input);
    std::string error;
    if (ext == ".glb" || ext == ".gltf") {
        const auto compiled = avernal::compile_gltf(input, out_dir, stem, &error);
        if (!compiled) {
            std::cerr << "compile failed: " << error << "\n";
            return EXIT_FAILURE;
        }
        std::cout << "Wrote " << compiled->model.generic_string() << "\n";
        for (const auto& path : compiled->meshes) {
            std::cout << "  mesh     " << path.generic_string() << "\n";
        }
        for (const auto& path : compiled->materials) {
            std::cout << "  material " << path.generic_string() << "\n";
        }
        for (const auto& path : compiled->textures) {
            std::cout << "  texture  " << path.generic_string() << "\n";
        }
        return EXIT_SUCCESS;
    }

    const auto compiled =
        avernal::compile_heightmap(input, out_dir, stem, heightmap_options, &error);
    if (!compiled) {
        std::cerr << "compile failed: " << error << "\n";
        return EXIT_FAILURE;
    }
    std::cout << "Wrote " << compiled->path.generic_string() << "\n";
    return EXIT_SUCCESS;
}
