#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"

#include <avernal/asset_compiler/compile.hpp>
#include <avernal/assets/asset_id.hpp>
#include <avernal/assets_render/avmat.hpp>
#include <avernal/assets_render/avmodel.hpp>
#include <avernal/assets_render/avtex.hpp>
#include <avernal/assets_render/image.hpp>
#include <avernal/assets_render/mesh_asset.hpp>
#include <avernal/render/mesh.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace avernal {
namespace {

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

[[nodiscard]] std::string posix_generic(const std::filesystem::path& path) {
    return path.generic_string();
}

[[nodiscard]] std::uint64_t asset_id_for(std::string_view relative_path) {
    return AssetId{relative_path}.id();
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
    while (!out.empty() && out.front() == '_') {
        out.erase(out.begin());
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    return out.empty() ? std::string{"unnamed"} : out;
}

[[nodiscard]] std::string unique_stem(std::unordered_set<std::string>& used, std::string stem) {
    if (used.insert(stem).second) {
        return stem;
    }
    for (int suffix = 2;; ++suffix) {
        auto candidate = stem + "_" + std::to_string(suffix);
        if (used.insert(candidate).second) {
            return candidate;
        }
    }
}

[[nodiscard]] std::string relative_to(const std::filesystem::path& path,
    const std::filesystem::path& base) {
    const auto relative = std::filesystem::relative(path, base);
    if (relative.empty()) {
        return posix_generic(path.filename());
    }
    return posix_generic(relative);
}

bool load_gltf_image(tinygltf::Image* image, const int, std::string* err, std::string*, int, int,
    const unsigned char* bytes, int size, void*) {
    if (image == nullptr || bytes == nullptr || size <= 0) {
        if (err != nullptr) {
            *err = "invalid glTF image payload";
        }
        return false;
    }

    const auto loaded = load_image_rgba8_from_memory(
        std::as_bytes(std::span{bytes, static_cast<std::size_t>(size)}));
    if (!loaded) {
        if (err != nullptr) {
            *err = "failed to decode glTF image";
        }
        return false;
    }

    image->width = static_cast<int>(loaded->width);
    image->height = static_cast<int>(loaded->height);
    image->component = 4;
    image->bits = 8;
    image->pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
    image->image.assign(loaded->pixels.begin(), loaded->pixels.end());
    return true;
}

[[nodiscard]] bool in_range(std::size_t offset, std::size_t size, std::size_t total) noexcept {
    return offset <= total && size <= total - offset;
}

[[nodiscard]] const std::uint8_t* accessor_bytes(const tinygltf::Model& model,
    const tinygltf::Accessor& accessor, std::size_t* stride, std::size_t* count) {
    if (accessor.bufferView < 0 ||
        static_cast<std::size_t>(accessor.bufferView) >= model.bufferViews.size()) {
        return nullptr;
    }
    const auto& view = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    if (view.buffer < 0 || static_cast<std::size_t>(view.buffer) >= model.buffers.size()) {
        return nullptr;
    }
    const auto& buffer = model.buffers[static_cast<std::size_t>(view.buffer)];
    const auto component_size = tinygltf::GetComponentSizeInBytes(accessor.componentType);
    const auto components = tinygltf::GetNumComponentsInType(accessor.type);
    if (component_size <= 0 || components <= 0) {
        return nullptr;
    }
    const auto default_stride = static_cast<std::size_t>(component_size * components);
    *stride = view.byteStride != 0 ? static_cast<std::size_t>(view.byteStride) : default_stride;
    *count = static_cast<std::size_t>(accessor.count);
    const auto offset = static_cast<std::size_t>(view.byteOffset) +
                        static_cast<std::size_t>(accessor.byteOffset);
    const auto needed = *count == 0 ? 0 : ((*count - 1) * *stride) + default_stride;
    if (!in_range(offset, needed, buffer.data.size())) {
        return nullptr;
    }
    return buffer.data.data() + offset;
}

[[nodiscard]] bool read_vec3(const tinygltf::Model& model, int accessor_index,
    std::vector<std::array<float, 3>>& out) {
    if (accessor_index < 0 ||
        static_cast<std::size_t>(accessor_index) >= model.accessors.size()) {
        return false;
    }
    const auto& accessor = model.accessors[static_cast<std::size_t>(accessor_index)];
    if (accessor.type != TINYGLTF_TYPE_VEC3 ||
        accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
        return false;
    }
    std::size_t stride = 0;
    std::size_t count = 0;
    const auto* bytes = accessor_bytes(model, accessor, &stride, &count);
    if (bytes == nullptr) {
        return false;
    }
    out.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        std::memcpy(out[i].data(), bytes + i * stride, sizeof(float) * 3);
    }
    return true;
}

[[nodiscard]] float read_normalized_component(
    const std::uint8_t* src, int component_type, bool normalized) {
    switch (component_type) {
    case TINYGLTF_COMPONENT_TYPE_FLOAT: {
        float value = 0.0f;
        std::memcpy(&value, src, sizeof(value));
        return value;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
        const float value = static_cast<float>(src[0]);
        return normalized ? value / 255.0f : value;
    }
    case TINYGLTF_COMPONENT_TYPE_BYTE: {
        const float value = static_cast<float>(static_cast<std::int8_t>(src[0]));
        return normalized ? std::max(value / 127.0f, -1.0f) : value;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
        std::uint16_t packed = 0;
        std::memcpy(&packed, src, sizeof(packed));
        const float value = static_cast<float>(packed);
        return normalized ? value / 65535.0f : value;
    }
    case TINYGLTF_COMPONENT_TYPE_SHORT: {
        std::int16_t packed = 0;
        std::memcpy(&packed, src, sizeof(packed));
        const float value = static_cast<float>(packed);
        return normalized ? std::max(value / 32767.0f, -1.0f) : value;
    }
    default:
        return 0.0f;
    }
}

[[nodiscard]] bool read_vec2(const tinygltf::Model& model, int accessor_index,
    std::vector<std::array<float, 2>>& out) {
    if (accessor_index < 0 ||
        static_cast<std::size_t>(accessor_index) >= model.accessors.size()) {
        return false;
    }
    const auto& accessor = model.accessors[static_cast<std::size_t>(accessor_index)];
    if (accessor.type != TINYGLTF_TYPE_VEC2) {
        return false;
    }
    const auto component_size = tinygltf::GetComponentSizeInBytes(accessor.componentType);
    if (component_size <= 0) {
        return false;
    }
    std::size_t stride = 0;
    std::size_t count = 0;
    const auto* bytes = accessor_bytes(model, accessor, &stride, &count);
    if (bytes == nullptr) {
        return false;
    }
    out.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto* src = bytes + i * stride;
        out[i][0] = read_normalized_component(src, accessor.componentType, accessor.normalized);
        out[i][1] = read_normalized_component(
            src + static_cast<std::size_t>(component_size), accessor.componentType, accessor.normalized);
    }
    return true;
}

[[nodiscard]] bool read_colors(const tinygltf::Model& model, int accessor_index,
    std::vector<std::array<float, 4>>& out) {
    if (accessor_index < 0 ||
        static_cast<std::size_t>(accessor_index) >= model.accessors.size()) {
        return false;
    }
    const auto& accessor = model.accessors[static_cast<std::size_t>(accessor_index)];
    std::size_t stride = 0;
    std::size_t count = 0;
    const auto* bytes = accessor_bytes(model, accessor, &stride, &count);
    if (bytes == nullptr) {
        return false;
    }
    out.assign(count, {1.0f, 1.0f, 1.0f, 1.0f});
    const auto components = tinygltf::GetNumComponentsInType(accessor.type);
    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT &&
        (accessor.type == TINYGLTF_TYPE_VEC3 || accessor.type == TINYGLTF_TYPE_VEC4)) {
        for (std::size_t i = 0; i < count; ++i) {
            std::memcpy(out[i].data(), bytes + i * stride, sizeof(float) * static_cast<std::size_t>(components));
            if (components < 4) {
                out[i][3] = 1.0f;
            }
        }
        return true;
    }
    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
        (accessor.type == TINYGLTF_TYPE_VEC3 || accessor.type == TINYGLTF_TYPE_VEC4)) {
        for (std::size_t i = 0; i < count; ++i) {
            const auto* pixel = bytes + i * stride;
            for (int c = 0; c < components; ++c) {
                out[i][static_cast<std::size_t>(c)] = static_cast<float>(pixel[c]) / 255.0f;
            }
            if (components < 4) {
                out[i][3] = 1.0f;
            }
        }
        return true;
    }
    return false;
}

[[nodiscard]] bool read_indices(const tinygltf::Model& model, int accessor_index,
    std::vector<std::uint32_t>& out) {
    if (accessor_index < 0 ||
        static_cast<std::size_t>(accessor_index) >= model.accessors.size()) {
        return false;
    }
    const auto& accessor = model.accessors[static_cast<std::size_t>(accessor_index)];
    if (accessor.type != TINYGLTF_TYPE_SCALAR) {
        return false;
    }
    std::size_t stride = 0;
    std::size_t count = 0;
    const auto* bytes = accessor_bytes(model, accessor, &stride, &count);
    if (bytes == nullptr) {
        return false;
    }
    out.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto* src = bytes + i * stride;
        switch (accessor.componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            out[i] = src[0];
            break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
            std::uint16_t value = 0;
            std::memcpy(&value, src, sizeof(value));
            out[i] = value;
            break;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            std::memcpy(&out[i], src, sizeof(std::uint32_t));
            break;
        default:
            return false;
        }
    }
    return true;
}

[[nodiscard]] float length3(float x, float y, float z) noexcept {
    return std::sqrt(x * x + y * y + z * z);
}

void normalize3(float& x, float& y, float& z) {
    const auto len = length3(x, y, z);
    if (len <= 1.0e-8f) {
        x = 0.0f;
        y = 1.0f;
        z = 0.0f;
        return;
    }
    x /= len;
    y /= len;
    z /= len;
}

void generate_normals(std::vector<render::Vertex>& vertices, const std::vector<std::uint32_t>& indices) {
    for (auto& vertex : vertices) {
        vertex.normal[0] = 0.0f;
        vertex.normal[1] = 0.0f;
        vertex.normal[2] = 0.0f;
    }
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
        const auto i0 = indices[i];
        const auto i1 = indices[i + 1];
        const auto i2 = indices[i + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
            continue;
        }
        auto& a = vertices[i0];
        auto& b = vertices[i1];
        auto& c = vertices[i2];
        const float e1[3] = {b.position[0] - a.position[0], b.position[1] - a.position[1],
            b.position[2] - a.position[2]};
        const float e2[3] = {c.position[0] - a.position[0], c.position[1] - a.position[1],
            c.position[2] - a.position[2]};
        const float n[3] = {
            e1[1] * e2[2] - e1[2] * e2[1],
            e1[2] * e2[0] - e1[0] * e2[2],
            e1[0] * e2[1] - e1[1] * e2[0],
        };
        for (auto* vertex : {&a, &b, &c}) {
            vertex->normal[0] += n[0];
            vertex->normal[1] += n[1];
            vertex->normal[2] += n[2];
        }
    }
    for (auto& vertex : vertices) {
        normalize3(vertex.normal[0], vertex.normal[1], vertex.normal[2]);
    }
}

void quat_from_matrix(const float m[9], float quat[4]) {
    const float trace = m[0] + m[4] + m[8];
    if (trace > 0.0f) {
        const float s = 0.5f / std::sqrt(trace + 1.0f);
        quat[3] = 0.25f / s;
        quat[0] = (m[7] - m[5]) * s;
        quat[1] = (m[2] - m[6]) * s;
        quat[2] = (m[3] - m[1]) * s;
        return;
    }
    if (m[0] > m[4] && m[0] > m[8]) {
        const float s = 2.0f * std::sqrt(std::max(0.0f, 1.0f + m[0] - m[4] - m[8]));
        quat[3] = (m[7] - m[5]) / s;
        quat[0] = 0.25f * s;
        quat[1] = (m[1] + m[3]) / s;
        quat[2] = (m[2] + m[6]) / s;
        return;
    }
    if (m[4] > m[8]) {
        const float s = 2.0f * std::sqrt(std::max(0.0f, 1.0f + m[4] - m[0] - m[8]));
        quat[3] = (m[2] - m[6]) / s;
        quat[0] = (m[1] + m[3]) / s;
        quat[1] = 0.25f * s;
        quat[2] = (m[5] + m[7]) / s;
        return;
    }
    const float s = 2.0f * std::sqrt(std::max(0.0f, 1.0f + m[8] - m[0] - m[4]));
    quat[3] = (m[3] - m[1]) / s;
    quat[0] = (m[2] + m[6]) / s;
    quat[1] = (m[5] + m[7]) / s;
    quat[2] = 0.25f * s;
}

void decompose_matrix(const std::vector<double>& matrix, float position[3], float rotation[4],
    float scale[3]) {
    position[0] = static_cast<float>(matrix[12]);
    position[1] = static_cast<float>(matrix[13]);
    position[2] = static_cast<float>(matrix[14]);

    const float c0[3] = {static_cast<float>(matrix[0]), static_cast<float>(matrix[1]),
        static_cast<float>(matrix[2])};
    const float c1[3] = {static_cast<float>(matrix[4]), static_cast<float>(matrix[5]),
        static_cast<float>(matrix[6])};
    const float c2[3] = {static_cast<float>(matrix[8]), static_cast<float>(matrix[9]),
        static_cast<float>(matrix[10])};
    scale[0] = length3(c0[0], c0[1], c0[2]);
    scale[1] = length3(c1[0], c1[1], c1[2]);
    scale[2] = length3(c2[0], c2[1], c2[2]);

    float rot[9] = {
        scale[0] > 1.0e-8f ? c0[0] / scale[0] : 1.0f,
        scale[0] > 1.0e-8f ? c0[1] / scale[0] : 0.0f,
        scale[0] > 1.0e-8f ? c0[2] / scale[0] : 0.0f,
        scale[1] > 1.0e-8f ? c1[0] / scale[1] : 0.0f,
        scale[1] > 1.0e-8f ? c1[1] / scale[1] : 1.0f,
        scale[1] > 1.0e-8f ? c1[2] / scale[1] : 0.0f,
        scale[2] > 1.0e-8f ? c2[0] / scale[2] : 0.0f,
        scale[2] > 1.0e-8f ? c2[1] / scale[2] : 0.0f,
        scale[2] > 1.0e-8f ? c2[2] / scale[2] : 1.0f,
    };
    quat_from_matrix(rot, rotation);
}

void convert_node_to_lh(float position[3], float rotation[4]) {
    position[2] = -position[2];
    rotation[2] = -rotation[2];
}

void node_trs(const tinygltf::Node& node, float position[3], float rotation[4], float scale[3]) {
    position[0] = 0.0f;
    position[1] = 0.0f;
    position[2] = 0.0f;
    rotation[0] = 0.0f;
    rotation[1] = 0.0f;
    rotation[2] = 0.0f;
    rotation[3] = 1.0f;
    scale[0] = 1.0f;
    scale[1] = 1.0f;
    scale[2] = 1.0f;

    if (node.matrix.size() == 16) {
        decompose_matrix(node.matrix, position, rotation, scale);
    } else {
        if (node.translation.size() >= 3) {
            position[0] = static_cast<float>(node.translation[0]);
            position[1] = static_cast<float>(node.translation[1]);
            position[2] = static_cast<float>(node.translation[2]);
        }
        if (node.rotation.size() >= 4) {
            rotation[0] = static_cast<float>(node.rotation[0]);
            rotation[1] = static_cast<float>(node.rotation[1]);
            rotation[2] = static_cast<float>(node.rotation[2]);
            rotation[3] = static_cast<float>(node.rotation[3]);
        }
        if (node.scale.size() >= 3) {
            scale[0] = static_cast<float>(node.scale[0]);
            scale[1] = static_cast<float>(node.scale[1]);
            scale[2] = static_cast<float>(node.scale[2]);
        }
    }
    convert_node_to_lh(position, rotation);
}

[[nodiscard]] int image_index_for_texture(const tinygltf::Model& model, int texture_index) {
    if (texture_index < 0 || static_cast<std::size_t>(texture_index) >= model.textures.size()) {
        return -1;
    }
    return model.textures[static_cast<std::size_t>(texture_index)].source;
}

struct PrimitiveKey {
    int mesh{};
    int primitive{};

    [[nodiscard]] bool operator==(const PrimitiveKey& other) const noexcept {
        return mesh == other.mesh && primitive == other.primitive;
    }
};

struct PrimitiveKeyHash {
    [[nodiscard]] std::size_t operator()(const PrimitiveKey& key) const noexcept {
        return (static_cast<std::size_t>(key.mesh) << 16) ^ static_cast<std::size_t>(key.primitive);
    }
};

}  // namespace

std::optional<CompiledGltf> compile_gltf(const std::filesystem::path& input,
    const std::filesystem::path& out_dir, std::string_view stem, std::string* error) {
    if (input.empty()) {
        set_error(error, "missing input path");
        return std::nullopt;
    }
    if (!std::filesystem::exists(input)) {
        set_error(error, "input file not found: " + input.string());
        return std::nullopt;
    }

    const auto extension = input.extension().string();
    const bool is_glb = extension == ".glb" || extension == ".GLB";
    const bool is_gltf = extension == ".gltf" || extension == ".GLTF";
    if (!is_glb && !is_gltf) {
        set_error(error, "expected a .glb or .gltf file");
        return std::nullopt;
    }

    auto model_stem = std::string{stem};
    if (model_stem.empty()) {
        model_stem = sanitize_stem(input.stem().string());
    } else {
        model_stem = sanitize_stem(model_stem);
    }

    tinygltf::TinyGLTF loader;
    loader.SetImageLoader(load_gltf_image, nullptr);

    tinygltf::Model gltf;
    std::string gltf_error;
    std::string gltf_warn;
    const bool loaded = is_glb ? loader.LoadBinaryFromFile(&gltf, &gltf_error, &gltf_warn, input.string())
                               : loader.LoadASCIIFromFile(&gltf, &gltf_error, &gltf_warn, input.string());
    if (!loaded) {
        set_error(error, gltf_error.empty() ? "failed to parse glTF" : gltf_error);
        return std::nullopt;
    }

    std::filesystem::create_directories(out_dir);
    const auto mesh_dir = out_dir / "meshes";
    const auto material_dir = out_dir / "materials";
    const auto texture_dir = out_dir / "textures";
    std::filesystem::create_directories(mesh_dir);
    std::filesystem::create_directories(material_dir);
    std::filesystem::create_directories(texture_dir);

    CompiledGltf compiled{};
    ModelDocument document{};

    std::unordered_set<std::string> mesh_stems;
    std::unordered_set<std::string> material_stems;
    std::unordered_set<std::string> texture_stems;
    std::unordered_map<int, std::uint32_t> image_to_texture;
    std::unordered_map<int, std::uint32_t> material_to_index;
    std::unordered_map<PrimitiveKey, std::uint32_t, PrimitiveKeyHash> primitive_to_mesh;

    const auto export_texture = [&](int image_index) -> std::uint32_t {
        if (image_index < 0 || static_cast<std::size_t>(image_index) >= gltf.images.size()) {
            return avmodel_none;
        }
        if (const auto it = image_to_texture.find(image_index); it != image_to_texture.end()) {
            return it->second;
        }

        const auto& image = gltf.images[static_cast<std::size_t>(image_index)];
        if (image.width <= 0 || image.height <= 0 || image.image.size() <
                static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4u) {
            return avmodel_none;
        }

        std::string name = image.name;
        if (name.empty() && !image.uri.empty()) {
            name = std::filesystem::path{image.uri}.stem().string();
        }
        if (name.empty()) {
            name = model_stem + "_tex_" + std::to_string(image_index);
        }

        TextureImage cooked{};
        cooked.width = static_cast<std::uint32_t>(image.width);
        cooked.height = static_cast<std::uint32_t>(image.height);
        cooked.pixels.assign(image.image.begin(),
            image.image.begin() +
                static_cast<std::ptrdiff_t>(cooked.width) * static_cast<std::ptrdiff_t>(cooked.height) * 4);

        const auto file_stem = unique_stem(texture_stems, sanitize_stem(name));
        const auto relative = posix_generic(std::filesystem::path{"textures"} / (file_stem + ".avtex"));
        const auto path = out_dir / relative;
        if (!save_avtex(path, cooked)) {
            return avmodel_none;
        }

        const auto index = static_cast<std::uint32_t>(compiled.textures.size());
        compiled.textures.push_back(path);
        image_to_texture.emplace(image_index, index);
        return index;
    };

    const auto export_texture_with_path = [&](int image_index, std::string& relative_out,
                                               std::uint64_t& id_out) -> bool {
        const auto index = export_texture(image_index);
        if (index == avmodel_none) {
            return false;
        }
        relative_out = posix_generic(std::filesystem::path{"textures"} /
                                     compiled.textures[index].filename());
        id_out = asset_id_for(relative_out);
        return true;
    };

    const auto export_material = [&](int material_index) -> std::uint32_t {
        const auto key = material_index;
        if (const auto it = material_to_index.find(key); it != material_to_index.end()) {
            return it->second;
        }

        MaterialDocument cooked{};
        cooked.flags = avmat_flag_use_3d | avmat_flag_use_depth;
        cooked.color[0] = 1.0f;
        cooked.color[1] = 1.0f;
        cooked.color[2] = 1.0f;
        cooked.color[3] = 1.0f;

        std::string name = model_stem + "_mat";
        int image_index = -1;
        if (material_index >= 0 && static_cast<std::size_t>(material_index) < gltf.materials.size()) {
            const auto& material = gltf.materials[static_cast<std::size_t>(material_index)];
            if (!material.name.empty()) {
                name = material.name;
            } else {
                name = model_stem + "_mat_" + std::to_string(material_index);
            }
            const auto& pbr = material.pbrMetallicRoughness;
            if (pbr.baseColorFactor.size() >= 4) {
                cooked.color[0] = static_cast<float>(pbr.baseColorFactor[0]);
                cooked.color[1] = static_cast<float>(pbr.baseColorFactor[1]);
                cooked.color[2] = static_cast<float>(pbr.baseColorFactor[2]);
                cooked.color[3] = static_cast<float>(pbr.baseColorFactor[3]);
            }
            image_index = image_index_for_texture(gltf, pbr.baseColorTexture.index);
            if (material.doubleSided) {
                cooked.flags |= avmat_flag_two_sided;
            }
        } else {
            name = model_stem + "_default";
        }

        std::string texture_relative;
        std::uint64_t texture_id = 0;
        if (image_index >= 0 && export_texture_with_path(image_index, texture_relative, texture_id)) {
            cooked.flags |= avmat_flag_use_texture;
            cooked.texture_asset_id = texture_id;
        }

        const auto file_stem = unique_stem(material_stems, sanitize_stem(name));
        const auto relative = posix_generic(std::filesystem::path{"materials"} / (file_stem + ".avmat"));
        const auto path = out_dir / relative;
        if (!texture_relative.empty()) {
            cooked.texture_path = relative_to(out_dir / texture_relative, path.parent_path());
        }
        cooked.asset_id = asset_id_for(relative);
        if (!save_avmat(path, cooked)) {
            return avmodel_none;
        }

        const auto index = static_cast<std::uint32_t>(document.materials.size());
        document.materials.push_back(ModelAssetRef{.asset_id = cooked.asset_id, .path = relative});
        compiled.materials.push_back(path);
        material_to_index.emplace(key, index);
        return index;
    };

    const auto export_primitive = [&](int mesh_index, int primitive_index,
                                       const tinygltf::Primitive& primitive) -> std::uint32_t {
        const PrimitiveKey key{mesh_index, primitive_index};
        if (const auto it = primitive_to_mesh.find(key); it != primitive_to_mesh.end()) {
            return it->second;
        }

        const int mode = primitive.mode < 0 ? TINYGLTF_MODE_TRIANGLES : primitive.mode;
        if (mode != TINYGLTF_MODE_TRIANGLES) {
            return avmodel_none;
        }

        const auto position_it = primitive.attributes.find("POSITION");
        if (position_it == primitive.attributes.end()) {
            return avmodel_none;
        }

        std::vector<std::array<float, 3>> positions;
        if (!read_vec3(gltf, position_it->second, positions) || positions.empty()) {
            return avmodel_none;
        }

        std::vector<std::array<float, 3>> normals;
        bool have_normals = false;
        if (const auto normal_it = primitive.attributes.find("NORMAL");
            normal_it != primitive.attributes.end()) {
            have_normals = read_vec3(gltf, normal_it->second, normals);
        }

        std::vector<std::array<float, 2>> uvs;
        if (const auto uv_it = primitive.attributes.find("TEXCOORD_0");
            uv_it != primitive.attributes.end()) {
            (void)read_vec2(gltf, uv_it->second, uvs);
        }

        std::vector<std::array<float, 4>> colors;
        if (const auto color_it = primitive.attributes.find("COLOR_0");
            color_it != primitive.attributes.end()) {
            (void)read_colors(gltf, color_it->second, colors);
        }

        std::vector<std::uint32_t> indices;
        if (primitive.indices >= 0) {
            if (!read_indices(gltf, primitive.indices, indices)) {
                return avmodel_none;
            }
        } else {
            indices.resize(positions.size());
            for (std::uint32_t i = 0; i < indices.size(); ++i) {
                indices[i] = i;
            }
        }
        if (indices.size() < 3 || indices.size() % 3 != 0) {
            return avmodel_none;
        }

        std::vector<render::Vertex> vertices(positions.size());
        for (std::size_t i = 0; i < positions.size(); ++i) {
            auto& vertex = vertices[i];
            vertex.position[0] = positions[i][0];
            vertex.position[1] = positions[i][1];
            vertex.position[2] = -positions[i][2];
            if (have_normals && i < normals.size()) {
                vertex.normal[0] = normals[i][0];
                vertex.normal[1] = normals[i][1];
                vertex.normal[2] = -normals[i][2];
            } else {
                vertex.normal[0] = 0.0f;
                vertex.normal[1] = 1.0f;
                vertex.normal[2] = 0.0f;
            }
            if (i < uvs.size()) {
                // glTF 2.0 UV (0,0) is the upper-left of the image, matching D3D/Vulkan
                // sampling of stb-decoded textures.
                vertex.texcoord[0] = uvs[i][0];
                vertex.texcoord[1] = uvs[i][1];
            } else {
                vertex.texcoord[0] = 0.0f;
                vertex.texcoord[1] = 0.0f;
            }
            if (i < colors.size()) {
                vertex.color[0] = colors[i][0];
                vertex.color[1] = colors[i][1];
                vertex.color[2] = colors[i][2];
                vertex.color[3] = colors[i][3];
            } else {
                vertex.color[0] = 1.0f;
                vertex.color[1] = 1.0f;
                vertex.color[2] = 1.0f;
                vertex.color[3] = 1.0f;
            }
        }
        // Reflecting Z (glTF RH -> Avernal LH) reverses winding into the clockwise-from-
        // outside order Avernal's 3D pipelines cull with. Do not swap indices again.
        // glTF doubleSided materials disable culling via avmat_flag_two_sided.
        if (!have_normals) {
            generate_normals(vertices, indices);
        }

        const auto geometry = render::mesh_geometry_from_vertices(vertices, indices);
        std::string name;
        if (static_cast<std::size_t>(mesh_index) < gltf.meshes.size() &&
            !gltf.meshes[static_cast<std::size_t>(mesh_index)].name.empty()) {
            name = gltf.meshes[static_cast<std::size_t>(mesh_index)].name;
        } else {
            name = model_stem + "_mesh_" + std::to_string(mesh_index);
        }
        if (static_cast<std::size_t>(mesh_index) < gltf.meshes.size() &&
            gltf.meshes[static_cast<std::size_t>(mesh_index)].primitives.size() > 1) {
            name += "_" + std::to_string(primitive_index);
        }

        const auto file_stem = unique_stem(mesh_stems, sanitize_stem(name));
        const auto relative = posix_generic(std::filesystem::path{"meshes"} / (file_stem + ".avmesh"));
        const auto path = out_dir / relative;
        if (!save_avmesh(path, geometry)) {
            return avmodel_none;
        }

        const auto index = static_cast<std::uint32_t>(document.meshes.size());
        document.meshes.push_back(ModelAssetRef{.asset_id = asset_id_for(relative), .path = relative});
        compiled.meshes.push_back(path);
        primitive_to_mesh.emplace(key, index);
        return index;
    };

    std::vector<int> roots;
    if (!gltf.scenes.empty()) {
        const auto scene_index = gltf.defaultScene >= 0 ? gltf.defaultScene : 0;
        if (static_cast<std::size_t>(scene_index) < gltf.scenes.size()) {
            roots = gltf.scenes[static_cast<std::size_t>(scene_index)].nodes;
        }
    }
    if (roots.empty()) {
        std::unordered_set<int> children;
        for (const auto& node : gltf.nodes) {
            for (const int child : node.children) {
                children.insert(child);
            }
        }
        for (int i = 0; i < static_cast<int>(gltf.nodes.size()); ++i) {
            if (!children.contains(i)) {
                roots.push_back(i);
            }
        }
    }

    const auto visit = [&](auto&& self, int node_index, std::uint32_t parent) -> void {
        if (node_index < 0 || static_cast<std::size_t>(node_index) >= gltf.nodes.size()) {
            return;
        }
        const auto& node = gltf.nodes[static_cast<std::size_t>(node_index)];
        ModelNode exported{};
        exported.parent = parent;
        exported.name = node.name;
        node_trs(node, exported.position, exported.rotation, exported.scale);
        const auto our_index = static_cast<std::uint32_t>(document.nodes.size());
        document.nodes.push_back(exported);

        if (node.mesh >= 0 && static_cast<std::size_t>(node.mesh) < gltf.meshes.size()) {
            const auto& mesh = gltf.meshes[static_cast<std::size_t>(node.mesh)];
            for (int primitive_index = 0;
                 primitive_index < static_cast<int>(mesh.primitives.size()); ++primitive_index) {
                const auto& primitive = mesh.primitives[static_cast<std::size_t>(primitive_index)];
                const auto mesh_ref = export_primitive(node.mesh, primitive_index, primitive);
                if (mesh_ref == avmodel_none) {
                    continue;
                }
                const auto material_ref = export_material(primitive.material);
                document.parts.push_back(ModelPart{
                    .node = our_index,
                    .mesh = mesh_ref,
                    .material = material_ref,
                });
            }
        }

        for (const int child : node.children) {
            self(self, child, our_index);
        }
    };

    for (const int root : roots) {
        visit(visit, root, avmodel_none);
    }

    if (document.parts.empty()) {
        set_error(error, "glTF contained no triangle meshes");
        return std::nullopt;
    }

    const auto model_relative = model_stem + ".avmodel";
    const auto model_path = out_dir / model_relative;
    document.asset_id = asset_id_for(model_relative);
    if (!save_avmodel(model_path, document)) {
        set_error(error, "failed to write " + model_path.string());
        return std::nullopt;
    }
    compiled.model = model_path;
    return compiled;
}

}  // namespace avernal
