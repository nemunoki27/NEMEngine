#include "FBXImportTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/Import/MeshFileImporter.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>

// assimp
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace {

	using Bounds = std::array<float, 6>;

	// 各軸の最小と最大を更新する
	void Include(Bounds& bounds, const aiVector3D& vertex) {

		const float values[]{vertex.x, vertex.y, vertex.z};
		for (size_t axis = 0; axis < 3; ++axis) {
			bounds[axis] = (std::min)(bounds[axis], values[axis]);
			bounds[axis + 3] = (std::max)(bounds[axis + 3], values[axis]);
		}
	}
	// 未変換の頂点からモデル全体の配置を計算する
	void CollectBounds(const aiScene* scene, const aiNode* node, const aiMatrix4x4& parent, Bounds& bounds) {

		const auto matrix = parent * node->mTransformation;
		for (uint32_t index = 0; index < node->mNumMeshes; ++index) {
			const auto* mesh = scene->mMeshes[node->mMeshes[index]];
			for (uint32_t vertex = 0; vertex < mesh->mNumVertices; ++vertex) {
				const auto position = matrix * mesh->mVertices[vertex];
				Include(bounds, {-position.x, position.y, position.z});
			}
		}
		for (uint32_t index = 0; index < node->mNumChildren; ++index) {
			CollectBounds(scene, node->mChildren[index], matrix, bounds);
		}
	}
}

bool NEMTests::CheckFBXTransforms() {

	using namespace Engine;
	constexpr float maximum = std::numeric_limits<float>::max();
	const Bounds empty{maximum, maximum, maximum, -maximum, -maximum, -maximum};
	const auto fixtures = RuntimePaths::GetEngineProjectRoot() / "Externals/assimp/test/models/FBX";
	for (const char* name : {"box.fbx", "cubes_with_names.fbx", "cubes_with_mirroring_and_pivot.fbx"}) {
		const auto path = fixtures / name;
		Assimp::Importer importer;
		const auto* scene = importer.ReadFile(Algorithm::PathToUTF8(path), aiProcess_Triangulate);
		if (!scene || !scene->mRootNode) return false;
		auto expected = empty;
		CollectBounds(scene, scene->mRootNode, {}, expected);
		const auto mesh = MeshFileImporter::ImportFile({}, path, {}, {});
		auto actual = empty;
		for (const auto& vertex : mesh.vertices) {
			Include(actual, {vertex.position.x, vertex.position.y, vertex.position.z});
		}
		for (size_t axis = 0; axis < actual.size(); ++axis) {
			const float tolerance = 0.0001f * (std::max)(1.0f, std::fabs(expected[axis]));
			if (std::fabs(expected[axis] - actual[axis]) > tolerance) {
				std::cerr << "FBX transform mismatch: " << name << " axis=" << axis
					<< " expected=" << expected[axis] << " actual=" << actual[axis] << '\n';
				return false;
			}
		}
	}
	return true;
}
