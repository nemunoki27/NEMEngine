#include "FoundationTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Async/AssetWorkerPool.h>
#include <Engine/Core/Foundation/Math/AffineDecompose.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Paths/RuntimeAssetPaths.h>
#include <Engine/Core/Runtime/Paths/RuntimePathResolution.h>

// c++
#include <atomic>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>

namespace {

	static_assert(sizeof(Engine::Vector2) == 8 && offsetof(Engine::Vector2, y) == 4);
	static_assert(sizeof(Engine::Vector3) == 12 && offsetof(Engine::Vector3, z) == 8);
	static_assert(sizeof(Engine::Vector4) == 16 && offsetof(Engine::Vector4, w) == 12);
	static_assert(sizeof(Engine::Quaternion) == 16 && offsetof(Engine::Quaternion, w) == 12);
	static_assert(sizeof(Engine::Color3) == 12 && offsetof(Engine::Color3, b) == 8);
	static_assert(sizeof(Engine::Color4) == 16 && offsetof(Engine::Color4, a) == 12);

	// EditorとRuntimeで共有する分解が元の行列を再構成できるか確認する
	bool TestAffineDecomposition() {

		for (float angle : { 0.0f, 30.0f, 90.0f, 180.0f }) {
			for (float scaleZ : { 4.0f, -4.0f }) {
				const auto rotation = Engine::Quaternion::FromEulerDegrees({ angle, angle * 0.5f, -angle });
				const auto matrix = Engine::Matrix4x4::MakeAffineMatrix({ 2.0f, 3.0f, scaleZ }, rotation, { 5.0f, 6.0f, 7.0f });
				Engine::Vector3 position;
				Engine::Vector3 scale;
				Engine::Quaternion decomposedRotation;
				if (!Engine::DecomposeAffine3D(matrix, position, decomposedRotation, scale)) {
					return false;
				}
				const auto rebuilt = Engine::Matrix4x4::MakeAffineMatrix(scale, decomposedRotation, position);
				for (size_t row = 0; row < 4; ++row) {
					for (size_t column = 0; column < 4; ++column) {
						if (std::abs(matrix.m[row][column] - rebuilt.m[row][column]) > 0.0001f) {
							return false;
						}
					}
				}
			}
		}
		return true;
	}

	// 保存byteと失敗時の既存ファイル保持を確認する
	bool TestJsonStorage(const std::filesystem::path& root) {

		const auto filePath = root / Engine::Algorithm::PathFromUTF8("保存.json");
		const nlohmann::json data = { { "z", -0.0 }, { "a", { 1, 2, 3 } } };
		if (!Engine::JsonAdapter::SaveCanonical(filePath, data, 2)) {
			return false;
		}
		std::ifstream input(filePath, std::ios::binary);
		std::stringstream contents;
		contents << input.rdbuf();
		input.close();
		const auto savedTime = std::filesystem::last_write_time(filePath);
		if (contents.str() != "{\n  \"a\": [\n    1,\n    2,\n    3\n  ],\n  \"z\": 0.0\n}\n" ||
			!Engine::JsonAdapter::SaveCanonical(filePath, data, 2) ||
			std::filesystem::last_write_time(filePath) != savedTime) {
			return false;
		}
		const nlohmann::json invalid = { { "value", std::numeric_limits<double>::infinity() } };
		if (Engine::JsonAdapter::SaveCanonical(filePath, invalid) ||
			Engine::JsonAdapter::Load(filePath) != data ||
			Engine::JsonAdapter::SaveCanonical(filePath / "blocked.json", data)) {
			return false;
		}
		nlohmann::json values;
		Engine::JsonAdapter::SetVector3(values, "position", { 1.0f, 2.0f, 3.0f });
		const auto position = Engine::JsonAdapter::GetVector3(values, "position");
		values["position"]["z"] = "invalid";
		const auto fallback = Engine::JsonAdapter::GetVector3(values, "position", { 4.0f, 5.0f, 6.0f });
		return position.x == 1.0f && position.y == 2.0f && position.z == 3.0f &&
			fallback.x == 4.0f && fallback.y == 5.0f && fallback.z == 6.0f;
	}

	// 表示文字列とpathの不正UTF処理を区別する
	bool TestTextConversion() {

		const std::string text = "日本語/素材.png";
		if (Engine::Algorithm::PathToUTF8(Engine::Algorithm::PathFromUTF8(text)) != text ||
			Engine::Algorithm::ConvertString(Engine::Algorithm::ConvertString(text)) != text ||
			Engine::Algorithm::Utf8ToCodepoints("\xff") != std::vector<char32_t>{ 0xfffd }) {
			return false;
		}
		try {
			Engine::Algorithm::PathFromUTF8("\xff");
			return false;
		} catch (const std::runtime_error&) {
			return true;
		}
	}

	// 停止で待機中ジョブを処理し、再開後も完了を待てるか確認する
	bool TestWorkerCompletion() {

		Engine::AssetWorkerPool<uint32_t> workers;
		std::atomic<uint32_t> sum = 0;
		workers.Start(2, [&sum](uint32_t&& value, uint32_t) { sum.fetch_add(value); });
		for (uint32_t i = 1; i <= 128; ++i) {
			workers.Enqueue(i);
		}
		workers.Stop();
		if (sum != 8256 || !workers.IsIdle() || workers.GetStats().threadCount != 0) {
			return false;
		}
		workers.Start(1, [&sum](uint32_t&& value, uint32_t) { sum.fetch_add(value); });
		workers.Enqueue(7);
		workers.WaitIdle();
		const auto stats = workers.GetStats();
		return sum == 8263 && stats.queuedCount == 0 && stats.inFlightCount == 0 && stats.threadCount == 1;
	}

	// descriptor探索とURI変換をglobal状態の更新なしで検証する
	bool TestResolvedPaths(const std::filesystem::path& root) {

		const auto project = root / "Project";
		std::filesystem::create_directories(project / "GameAssets");
		std::ofstream(project / "b.nemproject") << "{}";
		std::ofstream(project / "a.nemproject") << "{}";
		if (Engine::RuntimePathDetail::FindProjectDescriptor(project / "GameAssets") !=
			Engine::RuntimePathDetail::NormalizePath(project / "a.nemproject")) {
			return false;
		}
		Engine::RuntimePaths::PathState state;
		state.projectRoot = state.gameRoot = project;
		state.gameAssetsRoot = project / "GameAssets";
		state.engineProjectRoot = root / "EngineProject";
		state.engineAssetsRoot = state.engineProjectRoot / "Engine/Assets";
		state.packages.push_back({ "com.test", "1", "embedded", root / "Package", 0 });
		const auto resolved = Engine::RuntimePathDetail::ResolveVirtualPath(state, "game://Scenes/test.scene.json");
		return resolved == state.gameAssetsRoot / "Scenes/test.scene.json" &&
			Engine::RuntimePathDetail::ToAssetPath(state, resolved) == "GameAssets/Scenes/test.scene.json" &&
			Engine::RuntimePathDetail::ResolveVirtualPath(state, "game://../escape").empty() &&
			Engine::RuntimePathDetail::ResolveVirtualPath(state, "package://com.test/data.json") == root / "Package/data.json" &&
			Engine::RuntimePathDetail::ResolveVirtualPath(state, "package://missing/data.json").empty();
	}
}

bool TestFoundationContracts() {

	try {
		const auto root = std::filesystem::temp_directory_path() / "NEMEngineTests/Foundation";
		std::filesystem::create_directories(root);
		const bool passed = TestAffineDecomposition() && TestJsonStorage(root) && TestTextConversion() &&
			TestWorkerCompletion() && TestResolvedPaths(root);
		if (!passed) {
			std::cerr << "Foundation contracts failed\n";
		}
		return passed;
	} catch (const std::exception& error) {
		std::cerr << "Foundation contracts exception: " << error.what() << '\n';
		return false;
	}
}
