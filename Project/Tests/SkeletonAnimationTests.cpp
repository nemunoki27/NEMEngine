#include "SkeletonAnimationTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/Animation/SkinnedMeshAnimationManager.h>
#include <Engine/Core/World/Systems/Animation/SkeletonPlaybackTime.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <array>
#include <algorithm>
#include <cmath>
#include <fstream>

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
