#include "ParticleMeshColorTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/Import/MeshFileImporter.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshUploadPreparation.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <array>
#include <fstream>

namespace {

	// 同じ位置を共有する色付きと色なしのPrimitiveを保存する
	bool WriteMesh(const std::filesystem::path& path, bool colored) {

		nlohmann::json document = nlohmann::json::parse(R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,1]}],
"nodes":[{"mesh":0},{"mesh":1}],"meshes":[{"name":"First","primitives":[{"attributes":{"POSITION":0}}]},
{"name":"Second","primitives":[{"attributes":{"POSITION":0}}]}],"buffers":[{"uri":"colors.bin","byteLength":84}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":48}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
{"bufferView":1,"componentType":5126,"count":3,"type":"VEC4"}]})");
		if (colored) document["meshes"][0]["primitives"][0]["attributes"]["COLOR_0"] = 1;
		std::ofstream stream(path);
		stream << document.dump();
		stream.close();
		return !stream.fail();
	}
}

bool NEMTests::TestParticleMeshVertexColors() {

	using namespace Engine;
	TestDirectory directory("ParticleVertexColors", RuntimePaths::GetGameAssetsRoot());
	const std::array<float, 21> binary{0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1};
	{
		std::ofstream stream(directory.GetPath() / "colors.bin", std::ios::binary);
		stream.write(reinterpret_cast<const char*>(binary.data()), sizeof(binary));
		stream.close();
		if (stream.fail()) return false;
	}
	const auto plainPath = directory.GetPath() / "plain.gltf";
	const auto coloredPath = directory.GetPath() / "colored.gltf";
	if (!WriteMesh(plainPath, false) || !WriteMesh(coloredPath, true)) return false;
	MeshImportSettings settings;
	settings.generateAutomaticLODs = true;
	const auto plain = MeshFileImporter::ImportFile({}, plainPath, settings, {});
	if (plain.vertices.size() != 6 || !plain.vertexColors.empty()) return false;
	const auto upload = MeshUploadPreparation::Build(plain);
	if (upload.hasVertexColors || upload.vertexColors.size() != 1 || upload.vertexColors[0] != Vector4(1, 1, 1, 1)) return false;
	const auto colored = MeshFileImporter::ImportFile({}, coloredPath, settings, {});
	if (colored.vertexColors.size() != colored.vertices.size()) return false;
	if (std::count(colored.vertexColors.begin(), colored.vertexColors.end(), Vector4(1, 0, 0, 1)) != 3 ||
		std::count(colored.vertexColors.begin(), colored.vertexColors.end(), Vector4(1, 1, 1, 1)) != 3) return false;
	for (bool reverse : {false, true}) {
		const std::array<std::filesystem::path, 3> lods{reverse ? plainPath : coloredPath, {}, {}};
		const auto imported = MeshFileImporter::ImportFile({}, reverse ? coloredPath : plainPath, settings, lods);
		if (imported.vertices.size() != 12 || imported.vertexColors.size() != 12 || !imported.authoredLODs[1]) return false;
		if (std::count(imported.vertexColors.begin(), imported.vertexColors.end(), Vector4(1, 0, 0, 1)) != 3 ||
			std::count(imported.vertexColors.begin(), imported.vertexColors.end(), Vector4(1, 1, 1, 1)) != 9) return false;
		const auto& lod = imported.lods[1];
		for (uint32_t index = lod.indexOffset; index < lod.indexOffset + lod.indexCount; ++index) {
			if (imported.indices[index] < 6 || imported.indices[index] >= 12) return false;
		}
		const auto prepared = MeshUploadPreparation::Build(imported);
		if (!prepared.hasVertexColors || prepared.vertexColors != imported.vertexColors) return false;
	}
	return true;
}
