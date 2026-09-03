#include <avernal/asset_compiler/compile.hpp>
#include <avernal/assets_render/avmat.hpp>
#include <avernal/assets_render/avmodel.hpp>
#include <avernal/assets_render/avtex.hpp>
#include <avernal/assets_render/image.hpp>
#include <avernal/assets_render/mesh_asset.hpp>
#include <avernal/render/mesh.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace {

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 24));
}

void append_bytes(std::vector<std::uint8_t>& out, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    out.insert(out.end(), bytes, bytes + size);
}

void pad4(std::vector<std::uint8_t>& out) {
    while (out.size() % 4 != 0) {
        out.push_back(0);
    }
}

void pad4(std::string& text) {
    while (text.size() % 4 != 0) {
        text.push_back(' ');
    }
}

// 1x1 transparent PNG used by multiple public-domain "smallest PNG" samples.
constexpr std::uint8_t kRedPng[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
    0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F,
    0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x00,
    0x01, 0x00, 0x00, 0x05, 0x00, 0x01, 0x0D, 0x0A, 0x2D, 0xB4, 0x00, 0x00, 0x00, 0x00, 0x49,
    0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
};

[[nodiscard]] std::vector<std::uint8_t> make_triangle_glb(bool with_texture) {
    const float positions[] = {
        -1.0f, -1.0f, 0.0f,
         1.0f, -1.0f, 0.0f,
         0.0f,  1.0f, 1.0f,
    };
    const float normals[] = {
        0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f,
    };
    const float uvs[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        0.5f, 1.0f,
    };
    const std::uint16_t indices[] = {0, 1, 2};

    std::vector<std::uint8_t> bin;
    append_bytes(bin, positions, sizeof(positions));
    append_bytes(bin, normals, sizeof(normals));
    append_bytes(bin, uvs, sizeof(uvs));
    append_bytes(bin, indices, sizeof(indices));
    std::size_t png_offset = 0;
    if (with_texture) {
        png_offset = bin.size();
        append_bytes(bin, kRedPng, sizeof(kRedPng));
    }
    const auto bin_length = bin.size();
    pad4(bin);

    std::string json = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],)"
                       R"("nodes":[{"mesh":0,"name":"Tri","translation":[0,1,2]}],)"
                       R"("meshes":[{"name":"Triangle","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0}]}],)"
                       R"("accessors":[)"
                       R"({"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-1,-1,0],"max":[1,1,1]},)"
                       R"({"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},)"
                       R"({"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},)"
                       R"({"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}],)"
                       R"("bufferViews":[)"
                       R"({"buffer":0,"byteOffset":0,"byteLength":36,"target":34962},)"
                       R"({"buffer":0,"byteOffset":36,"byteLength":36,"target":34962},)"
                       R"({"buffer":0,"byteOffset":72,"byteLength":24,"target":34962},)"
                       R"({"buffer":0,"byteOffset":96,"byteLength":6,"target":34963})";

    if (with_texture) {
        json += R"(,{"buffer":0,"byteOffset":)" + std::to_string(png_offset) +
                R"(,"byteLength":)" + std::to_string(sizeof(kRedPng)) + "}";
        json += R"(],"buffers":[{"byteLength":)" + std::to_string(bin_length) + "}],";
        json += R"("images":[{"bufferView":4,"mimeType":"image/png","name":"red"}],)"
                R"("textures":[{"source":0}],)"
                R"("materials":[{"name":"Red","doubleSided":true,"pbrMetallicRoughness":{"baseColorFactor":[1,0,0,1],"baseColorTexture":{"index":0}}}]})";
    } else {
        json += R"(],"buffers":[{"byteLength":)" + std::to_string(bin_length) + "}],";
        json += R"("materials":[{"name":"Red","pbrMetallicRoughness":{"baseColorFactor":[1,0,0,1]}}]})";
    }
    pad4(json);

    std::vector<std::uint8_t> glb;
    const auto total = static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + bin.size());
    append_u32(glb, 0x46546C67);
    append_u32(glb, 2);
    append_u32(glb, total);
    append_u32(glb, static_cast<std::uint32_t>(json.size()));
    append_u32(glb, 0x4E4F534A);
    append_bytes(glb, json.data(), json.size());
    append_u32(glb, static_cast<std::uint32_t>(bin.size()));
    append_u32(glb, 0x004E4942);
    append_bytes(glb, bin.data(), bin.size());
    return glb;
}

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

TEST(CompileGltf, MissingFileFails) {
    std::string error;
    const auto result = avernal::compile_gltf("no-such-file.glb", std::filesystem::temp_directory_path(),
        "missing", &error);
    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(error.empty());
}

TEST(CompileGltf, CompilesTriangleGlb) {
    const auto dir = std::filesystem::temp_directory_path() / "avernal-asset-compiler-triangle";
    std::filesystem::remove_all(dir);
    const auto glb_path = dir / "triangle.glb";
    ASSERT_TRUE(write_file(glb_path, make_triangle_glb(false)));

    std::string error;
    const auto compiled = avernal::compile_gltf(glb_path, dir / "out", "triangle", &error);
    ASSERT_TRUE(compiled.has_value()) << error;
    ASSERT_TRUE(std::filesystem::exists(compiled->model));
    ASSERT_EQ(compiled->meshes.size(), 1u);
    ASSERT_EQ(compiled->materials.size(), 1u);
    EXPECT_TRUE(compiled->textures.empty());

    const auto model = avernal::load_avmodel(compiled->model);
    ASSERT_TRUE(model.has_value());
    ASSERT_EQ(model->nodes.size(), 1u);
    EXPECT_EQ(model->nodes[0].name, "Tri");
    EXPECT_FLOAT_EQ(model->nodes[0].position[0], 0.0f);
    EXPECT_FLOAT_EQ(model->nodes[0].position[1], 1.0f);
    EXPECT_FLOAT_EQ(model->nodes[0].position[2], -2.0f);
    ASSERT_EQ(model->parts.size(), 1u);
    ASSERT_EQ(model->meshes.size(), 1u);
    ASSERT_EQ(model->materials.size(), 1u);

    const auto mesh = avernal::load_avmesh(compiled->meshes[0]);
    ASSERT_TRUE(mesh.has_value());
    ASSERT_FALSE(mesh->stream_data.empty());
    ASSERT_GE(mesh->stream_data[0].size(), sizeof(avernal::render::Vertex) * 3);
    avernal::render::Vertex vertices[3]{};
    std::memcpy(vertices, mesh->stream_data[0].data(), sizeof(vertices));
    EXPECT_FLOAT_EQ(vertices[2].position[2], -1.0f);
    EXPECT_FLOAT_EQ(vertices[0].normal[2], -1.0f);
    EXPECT_FLOAT_EQ(vertices[0].texcoord[1], 0.0f);
    EXPECT_FLOAT_EQ(vertices[2].texcoord[1], 1.0f);

    ASSERT_EQ(mesh->index_data.size(), 6u);
    std::uint16_t indices[3]{};
    std::memcpy(indices, mesh->index_data.data(), sizeof(indices));
    EXPECT_EQ(indices[0], 0);
    EXPECT_EQ(indices[1], 1);
    EXPECT_EQ(indices[2], 2);

    const auto material = avernal::load_avmat(compiled->materials[0]);
    ASSERT_TRUE(material.has_value());
    EXPECT_FLOAT_EQ(material->color[0], 1.0f);
    EXPECT_FLOAT_EQ(material->color[1], 0.0f);
    EXPECT_FLOAT_EQ(material->color[2], 0.0f);
    EXPECT_EQ(material->flags & avernal::avmat_flag_use_3d, avernal::avmat_flag_use_3d);
    EXPECT_EQ(material->flags & avernal::avmat_flag_use_texture, 0u);
    EXPECT_EQ(material->flags & avernal::avmat_flag_two_sided, 0u);

    std::filesystem::remove_all(dir);
}

TEST(CompileGltf, CompilesTexturedTriangleGlb) {
    const auto decoded = avernal::load_image_rgba8_from_memory(std::as_bytes(std::span{kRedPng}));
    if (!decoded) {
        GTEST_SKIP() << "embedded test PNG did not decode";
    }

    const auto dir = std::filesystem::temp_directory_path() / "avernal-asset-compiler-textured";
    std::filesystem::remove_all(dir);
    const auto glb_path = dir / "textured.glb";
    ASSERT_TRUE(write_file(glb_path, make_triangle_glb(true)));

    std::string error;
    const auto compiled = avernal::compile_gltf(glb_path, dir / "out", "textured", &error);
    ASSERT_TRUE(compiled.has_value()) << error;
    ASSERT_EQ(compiled->textures.size(), 1u);

    const auto material = avernal::load_avmat(compiled->materials[0]);
    ASSERT_TRUE(material.has_value());
    EXPECT_EQ(material->flags & avernal::avmat_flag_use_texture, avernal::avmat_flag_use_texture);
    EXPECT_EQ(material->flags & avernal::avmat_flag_two_sided, avernal::avmat_flag_two_sided);
    EXPECT_FALSE(material->texture_path.empty());

    const auto texture = avernal::load_avtex(compiled->textures[0]);
    ASSERT_TRUE(texture.has_value());
    EXPECT_EQ(texture->width, 1u);
    EXPECT_EQ(texture->height, 1u);
    ASSERT_EQ(texture->pixels.size(), 4u);

    std::filesystem::remove_all(dir);
}
