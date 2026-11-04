#include "SkeletonAnimationTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/Animation/SkinnedMeshAnimationManager.h>
#include <Engine/Core/Rendering/Meshes/Import/MeshFileImporter.h>
#include <Engine/Core/Rendering/Meshes/Import/ModelFileIOSystem.h>
#include <Engine/Core/Rendering/Meshes/Import/ModelFileDependencyCollector.h>
#include <Engine/Core/World/Systems/Animation/SkeletonPoseEvaluator.h>
#include <Engine/Core/World/Systems/Animation/SkeletonPlaybackTime.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <array>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

// assimp
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace {

	bool WriteAnimatedModel(const std::filesystem::path& path, float duration) {

		std::ofstream buffer(path.parent_path() / "mesh.bin", std::ios::binary);
		if (!buffer) return false;
		const auto write = [&](const auto& values) {

			buffer.write(reinterpret_cast<const char*>(values.data()), sizeof(values));
		};
		// 三角形を1本の骨で動かす最小モデルを作る
		write(std::array<float, 9>{ 0, 0, 0, 1, 0, 0, 0, 1, 0 });
		write(std::array<uint16_t, 12>{});
		write(std::array<float, 12>{ 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 });
		write(std::array<uint16_t, 4>{ 0, 1, 2, 0 });
		write(std::array<float, 16>{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 });
		write(std::array<float, 2>{ 0, duration });
		write(std::array<float, 6>{ 0, 0, 0, 0, 1, 0 });
		buffer.close();
		if (buffer.fail()) return false;
		std::ofstream model(path, std::ios::binary);
		model << R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],
"nodes":[{"name":"Root","children":[1,2]},{"name":"Joint"},{"name":"Mesh","mesh":0,"skin":0}],
"skins":[{"inverseBindMatrices":4,"skeleton":0,"joints":[1]}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":1,"WEIGHTS_0":2},"indices":3}]}],
"buffers":[{"uri":"mesh.bin","byteLength":212}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},
{"buffer":0,"byteOffset":36,"byteLength":24},{"buffer":0,"byteOffset":60,"byteLength":48},
{"buffer":0,"byteOffset":108,"byteLength":6},{"buffer":0,"byteOffset":116,"byteLength":64},
{"buffer":0,"byteOffset":180,"byteLength":8},{"buffer":0,"byteOffset":188,"byteLength":24}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
{"bufferView":1,"componentType":5123,"count":3,"type":"VEC4"},
{"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},
{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"},
{"bufferView":4,"componentType":5126,"count":1,"type":"MAT4"},
{"bufferView":5,"componentType":5126,"count":2,"type":"SCALAR"},
{"bufferView":6,"componentType":5126,"count":2,"type":"VEC3"}],
"animations":[{"name":"Move","samplers":[{"input":5,"output":6}],
"channels":[{"sampler":0,"target":{"node":1,"path":"translation"}}]}]})";
		model.close();
		return !model.fail();
	}

	// 元のノード階層からモデル空間の行列を求める
	aiMatrix4x4 SourceNodeMatrix(const aiNode* node) {

		return node->mParent ? SourceNodeMatrix(node->mParent) * node->mTransformation : node->mTransformation;
	}

	// Assimpの元データと実際のスキニング入力を頂点単位で照合する
	bool CheckModelPose(const std::filesystem::path& path, bool animated) {

		using namespace Engine;
		AssetDatabase database;
		if (!database.Init()) return false;
		const auto id = database.ImportOrGet(RuntimePaths::ToAssetPath(path), AssetType::Mesh);
		SkinnedMeshAnimationManager manager;
		manager.Init(1);
		manager.RequestLoadAsync(database, id);
		manager.WaitAll();
		const auto animation = manager.Find(id);
		const auto mesh = MeshFileImporter::ImportFile(id, path, false);
		if (!animation || !animation->valid || !mesh.isSkinned || mesh.boneCount != animation->skeleton.joints.size()) {
			std::cerr << "Model definition mismatch: animation=" << bool(animation) << " skinned=" << mesh.isSkinned << " bones=" << mesh.boneCount << '\n';
			return false;
		}
		auto skeleton = animation->skeleton;
		std::vector<WellForGPU> palette;
		SkeletonPoseEvaluator::UpdateSkeletonHierarchy(skeleton);
		SkeletonPoseEvaluator::BuildPalette(skeleton, animation->skinCluster, palette);
		Assimp::Importer importer;
		auto* files = new ModelFileIOSystem(path);
		importer.SetIOHandler(files);
		const auto* scene = importer.ReadFile(files->GetModelPath(), aiProcess_Triangulate | aiProcess_PopulateArmatureData |
			aiProcess_FlipWindingOrder | aiProcess_FlipUVs | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace |
			aiProcess_JoinIdenticalVertices | aiProcess_ImproveCacheLocality | aiProcess_SortByPType);
		if (!scene) return false;
		uint32_t offset = 0, attachments = 0;
		for (uint32_t index = 0; index < scene->mNumMeshes; ++index) {
			const auto* source = scene->mMeshes[index];
			std::vector<aiVector3D> expected(source->mNumVertices);
			if (source->HasBones()) {
				for (uint32_t boneIndex = 0; boneIndex < source->mNumBones; ++boneIndex) {
					const auto* bone = source->mBones[boneIndex];
					if (!bone->mNode) return false;
					const auto matrix = SourceNodeMatrix(bone->mNode) * bone->mOffsetMatrix;
					for (uint32_t weight = 0; weight < bone->mNumWeights; ++weight) {
						const auto& influence = bone->mWeights[weight];
						expected[influence.mVertexId] += matrix * source->mVertices[influence.mVertexId] * influence.mWeight;
					}
				}
			} else {
				++attachments;
				const aiNode* owner = nullptr;
				auto find = [&](auto&& self, const aiNode* node) -> void {
					for (uint32_t meshIndex = 0; meshIndex < node->mNumMeshes; ++meshIndex) if (node->mMeshes[meshIndex] == index) owner = node;
					for (uint32_t child = 0; child < node->mNumChildren; ++child) self(self, node->mChildren[child]);
				};
				find(find, scene->mRootNode);
				if (!owner) return false;
				const auto matrix = SourceNodeMatrix(owner);
				for (uint32_t vertex = 0; vertex < source->mNumVertices; ++vertex) expected[vertex] = matrix * source->mVertices[vertex];
			}
			for (uint32_t vertex = 0; vertex < source->mNumVertices; ++vertex) {
				const auto& original = mesh.vertices[offset + vertex].position;
				const Vector3 position(original.x, original.y, original.z);
				const auto& influence = mesh.vertexInfluences[offset + vertex];
				Vector3 actual{};
				float sum = 0;
				for (uint32_t slot = 0; slot < kNumMaxInfluence; ++slot) {
					if (influence.weights[slot] <= 0) continue;
					actual += Vector3::Transform(position, palette[influence.jointIndices[slot]].skeletonSpaceMatrix) * influence.weights[slot];
					sum += influence.weights[slot];
				}
				if (sum > 0) actual /= sum;
				else actual = position;
				const Vector3 reference(-expected[vertex].x, expected[vertex].y, expected[vertex].z);
				if ((actual - reference).Length() > 0.002f * (std::max)(1.0f, reference.Length())) {
					std::cerr << "Model pose mismatch: " << source->mName.C_Str() << " vertex=" << vertex
						<< " actual=" << actual.x << ',' << actual.y << ',' << actual.z
						<< " expected=" << reference.x << ',' << reference.y << ',' << reference.z << '\n';
					return false;
				}
			}
			offset += source->mNumVertices;
		}
		if (animated) {
			skeleton = animation->skeleton;
			SkeletonPoseEvaluator::ApplyClipToSkeleton(skeleton, animation->clipJointTracks.at("Move"), 0.5f);
			SkeletonPoseEvaluator::UpdateSkeletonHierarchy(skeleton);
			SkeletonPoseEvaluator::BuildPalette(skeleton, animation->skinCluster, palette);
			const auto sword = skeleton.jointMap.find("Sword");
			if (sword == skeleton.jointMap.end()) return false;
			const auto position = Vector3::Transform({}, palette[sword->second].skeletonSpaceMatrix);
			if ((position - Vector3(-4, 3.5f, 0)).Length() > 0.001f) {
				std::cerr << "Attachment motion mismatch: " << position.x << ',' << position.y << ',' << position.z << '\n';
				return false;
			}
			for (const auto& [name, track] : animation->clips.at("Move").nodeAnimations) {
				if (track.translate.keyframes.size() != 2) { std::cerr << "Duplicate animation keys: " << track.translate.keyframes.size() << '\n'; return false; }
			}
		}
		std::cout << "Model pose verified: vertices=" << mesh.vertices.size() << " rigidParts=" << attachments << '\n';
		return attachments > 0;
	}

	// 回転と非一様Scaleを持つ剣が手の移動へ追従する
	bool CheckRigidAttachment(const std::filesystem::path& root) {

		using namespace Engine;
		const auto path = root / "attachment.gltf";
		if (!WriteAnimatedModel(path, 1)) return false;
		auto document = JsonFile::Load(path, false);
		document["nodes"][0]["translation"] = {0, 3, 0};
		document["nodes"][1]["translation"] = {0, 2, 0};
		document["nodes"][1]["children"] = {3};
		document["nodes"].push_back({{"name", "Sword"}, {"mesh", 1}, {"translation", {4, 0, 0}},
			{"scale", {2, 1, 1}}, {"rotation", {0, 0, std::sqrt(0.5f), std::sqrt(0.5f)}}});
		document["meshes"].push_back({{"primitives", {{{"attributes", {{"POSITION", 0}}}, {"indices", 3}}}}});
		document["animations"][0]["channels"].push_back(document["animations"][0]["channels"][0]);
		std::ofstream(path) << document.dump();
		return CheckModelPose(path, true);
	}

	bool TestPlaybackTime() {

		using Engine::SkeletonPlaybackTime::Advance;
		int32_t repeats = 0;
		bool finished = false;
		if (Advance(0.25f, 1.0f, 3.5f, true, repeats, finished) != 0.75f || repeats != 3 || finished) return false;
		repeats = 0;
		if (Advance(1.0f, 1.0f, -2.25f, true, repeats, finished) != 0.75f || repeats != 2 || finished) return false;
		if (Advance(0.25f, 1.0f, -0.5f, false, repeats, finished) != 0.0f || !finished) return false;
		if (Advance(0.0f, 1.0f, 0.0f, false, repeats, finished) != 0.0f || !finished) return false;
		return Advance(0.0f, 1.0f, 0.5f, false, repeats, finished) == 0.5f && !finished;
	}
}

bool NEMTests::TestSkeletonAnimation() {

	using namespace Engine;
	if (!TestPlaybackTime()) return false;
	TestDirectory directory("SkeletonAnimation", RuntimePaths::GetGameAssetsRoot());
	if (!CheckRigidAttachment(directory.GetPath())) return false;
	const auto external = Algorithm::GetEnvironmentPath(L"NEM_TEST_GLTF_MODEL");
	if (!external.empty()) {
		// ユーザーのモデルは読み取りだけ行い、検証用コピーで評価する
		TestDirectory copied("ModelPose", RuntimePaths::GetGameAssetsRoot());
		std::vector<std::filesystem::path> dependencies;
		std::string message;
		if (!ModelFileDependencyCollector::Collect(external, dependencies, message)) return false;
		for (const auto& file : dependencies) {
			const auto relative = std::filesystem::relative(file, external.parent_path());
			if (relative.empty() || relative.has_root_path() || *relative.begin() == "..") return false;
			const auto destination = copied.GetPath() / relative;
			std::filesystem::create_directories(destination.parent_path());
			std::filesystem::copy_file(file, destination);
		}
		if (!CheckModelPose(copied.GetPath() / external.filename(), false)) return false;
	}
	const auto path = directory.GetPath() / "mesh.gltf";
	if (!WriteAnimatedModel(path, 1.0f)) return false;
	AssetDatabase database;
	if (!database.Init()) return false;
	const AssetID mesh = database.ImportOrGet(RuntimePaths::ToAssetPath(path), AssetType::Mesh);
	if (!mesh) return false;
	SkinnedMeshAnimationManager manager;
	manager.Init(1);
	manager.RequestLoadAsync(database, mesh);
	manager.WaitAll();
	const auto original = manager.Find(mesh);
	if (!original || !original->valid || !original->clips.contains("Move") ||
		std::abs(original->clips.at("Move").duration - 1.0f) > 0.001f) return false;
	// 失敗時は旧世代を公開し続ける
	std::ofstream(path) << "{";
	database.NotifyContentChanged(mesh);
	manager.RequestLoadAsync(database, mesh);
	manager.WaitAll();
	if (manager.Find(mesh) != original) return false;
	if (!WriteAnimatedModel(path, 2.0f)) return false;
	database.NotifyContentChanged(mesh);
	manager.RequestLoadAsync(database, mesh);
	manager.WaitAll();
	const auto updated = manager.Find(mesh);
	if (!updated || updated->generation == original->generation ||
		std::abs(updated->clips.at("Move").duration - 2.0f) > 0.001f) return false;
	// Manager終了後も使用中の世代とTrackを保持する
	manager.Finalize();
	const auto& tracks = original->clipJointTracks.at("Move");
	return original->clips.at("Move").duration == 1.0f &&
		std::any_of(tracks.begin(), tracks.end(), [](const auto* track) {

			return track && !track->translate.keyframes.empty();
		});
}
