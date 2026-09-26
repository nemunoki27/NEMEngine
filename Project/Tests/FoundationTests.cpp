#include "FoundationTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Async/AssetWorkerPool.h>
#include <Engine/Core/Foundation/Math/AffineDecompose.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Math/Math.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFileJournal.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/Flipbook/FlipbookFrame.h>
#include <Engine/Core/Runtime/Paths/RuntimeAssetPaths.h>
#include <Engine/Core/Runtime/Packages/PackageResolution.h>
#include <Engine/Core/Platform/Input/InputSystem.h>
#include <Engine/Core/Platform/Windows/WindowInputBridge.h>
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

	// 行列積と回転演算、配列の座標順を確認する
	bool TestMathContracts() {

		const auto matrix = Engine::Matrix4x4::MakeAffineMatrix({ 2.0f, 3.0f, 4.0f },
			Engine::Quaternion::FromEulerDegrees({ 10.0f, 20.0f, 30.0f }), { 5.0f, 6.0f, 7.0f });
		auto multiplied = matrix;
		multiplied *= multiplied;
		if (multiplied != matrix * matrix) {
			return false;
		}
		Engine::Matrix4x4 inverse = Engine::Matrix4x4::Identity();
		if (!Engine::Matrix4x4::TryInverse(matrix, inverse)) {
			return false;
		}
		const auto identity = matrix * inverse;
		for (int row = 0; row < 4; ++row) {
			for (int column = 0; column < 4; ++column) {
				if (std::abs(identity.m[row][column] - (row == column ? 1.0f : 0.0f)) > 0.00001f) {
					return false;
				}
			}
		}
		const auto previous = inverse;
		if (Engine::Matrix4x4::TryInverse({}, inverse) || inverse != previous) {
			return false;
		}
		const Engine::Quaternion value{ 2.0f, 3.0f, 4.0f, 5.0f };
		const auto restored = value / Engine::Quaternion::Identity();
		if (restored != value || value - 1.0f != Engine::Quaternion{ 1.0f, 2.0f, 3.0f, 4.0f }) {
			return false;
		}
		if (Engine::Vector3::FromJson(nlohmann::json::array({ 1, 2, 3 })) != Engine::Vector3{ 1, 2, 3 } ||
			Engine::Quaternion::FromJson(nlohmann::json::array({ 2, 3, 4, 5 })) != value ||
			Engine::Quaternion::FromJson(value.ToJson()) != value) {
			return false;
		}
		// 巨大値と逆回転の失敗でNaNを公開しない
		Engine::Quaternion inverseRotation = value;
		if (Engine::Quaternion::TryInverse({ 0, 0, 0, 0 }, inverseRotation) || inverseRotation != value ||
			!Engine::Quaternion::TryInverse({ std::numeric_limits<float>::max(), 0, 0, 0 }, inverseRotation) ||
			!std::isfinite(inverseRotation.x) || inverseRotation.x >= 0.0f ||
			Engine::Vector3::Normalize({ std::numeric_limits<float>::max(), 0, 0 }) != Engine::Vector3{ 1, 0, 0 } ||
			Engine::Vector3::Normalize({ std::numeric_limits<float>::quiet_NaN(), 0, 0 }) != Engine::Vector3{}) {
			return false;
		}
		// 総数が32bitを超えるFlipbookでも末尾の行を選ぶ
		const std::vector<int32_t> tiles(3, (std::numeric_limits<int32_t>::max)());
		const auto lastFrame = Engine::CalcFlipbookFrame(tiles, 3, 1.0f);
		const auto invalidFrame = Engine::CalcFlipbookFrame(tiles, 3, std::numeric_limits<float>::quiet_NaN());
		if (std::abs(lastFrame.uvOffset.y - 2.0f / 3.0f) > 0.00001f || invalidFrame.uvOffset.x != 0.0f || invalidFrame.uvOffset.y != 0.0f) {
			return false;
		}
		const float large = Math::WrapDegree360(std::numeric_limits<float>::max());
		const auto normalized = Engine::Quaternion::Normalize({ std::numeric_limits<float>::max(), 0, 0, 0 });
		const auto blended = Engine::Quaternion::Lerp({ 0, 0, 0, 2 }, { 0, 0, 0, -3 }, 0.5f);
		if (normalized != Engine::Quaternion{ 1, 0, 0, 0 } || std::abs(blended.Length() - 1.0f) > 0.00001f ||
			Engine::Quaternion::Normalize({ 0, 0, 0, 0 }) != Engine::Quaternion::Identity()) {
			return false;
		}
		return large >= 0.0f && large < 360.0f && Math::WrapDegree360(-90.0f) == 270.0f &&
			Math::WrapDegree360(360.0f) == 0.0f &&
			std::isinf(Math::WrapDegree360(std::numeric_limits<float>::infinity())) &&
			std::isnan(Math::WrapDegree360(std::numeric_limits<float>::quiet_NaN()));
	}

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
		// 失敗時の出力保持とせん断の近似判定を確認する
		Engine::Vector3 position{ 1, 2, 3 };
		Engine::Vector3 scale{ 4, 5, 6 };
		Engine::Quaternion rotation = Engine::Quaternion::Identity();
		auto matrix = Engine::Matrix4x4::Identity();
		matrix.m[0][0] = std::numeric_limits<float>::quiet_NaN();
		if (Engine::DecomposeAffine3DResult(matrix, position, rotation, scale) != Engine::AffineDecompositionResult::Failed ||
			position != Engine::Vector3{ 1, 2, 3 } || scale != Engine::Vector3{ 4, 5, 6 } || rotation != Engine::Quaternion::Identity()) {
			return false;
		}
		matrix = Engine::Matrix4x4::Identity();
		matrix.m[0][1] = 0.5f;
		return Engine::DecomposeAffine3DResult(matrix, position, rotation, scale) == Engine::AffineDecompositionResult::Approximate;
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
		// nullは正常な文書として読み、破損時は読込先を維持する
		const auto nullPath = root / "null.json";
		nlohmann::json loaded = data;
		std::string diagnostic;
		if (!Engine::JsonAdapter::Save(nullPath, nullptr) || !Engine::JsonAdapter::TryLoad(nullPath, loaded, &diagnostic) ||
			!loaded.is_null() || !diagnostic.empty()) {
			return false;
		}
		std::ofstream(nullPath) << "null trailing";
		loaded = data;
		if (Engine::JsonAdapter::TryLoad(nullPath, loaded, &diagnostic) || loaded != data || diagnostic.empty() ||
			Engine::JsonAdapter::TryLoad(root / "missing.json", loaded) || loaded != data) {
			return false;
		}
		const nlohmann::json invalidText = {{ "text", std::string("\xff") }};
		if (Engine::JsonAdapter::SaveCanonical(filePath, invalidText) || Engine::JsonAdapter::Load(filePath) != data) {
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

	// 後続ファイルの保存失敗で先行ファイルを復元する
	bool TestJsonJournal(const std::filesystem::path& root) {

		const auto first = root / "first.json";
		const auto second = root / "second.json";
		const nlohmann::json original = {{ "value", 1 }};
		if (!Engine::JsonAdapter::SaveCanonical(first, original) || !Engine::JsonAdapter::SaveCanonical(second, original)) return false;
		const auto before = Engine::StorageFileUtility::FileRevision(first);
		Engine::JsonFileJournal::Scope scope{ root / "Recovery", [first, second](const auto& path) {
			return path == first || path == second;
		} };
		const std::vector<Engine::JsonFileChange> changes{ {first, {{ "value", 2 }}}, {second, {{ "value", 3 }}} };
		auto recover = [&scope](const auto& directory, std::string& error) {
			return Engine::JsonFileJournal::Recover(scope, directory, error, [](const auto&) {});
		};
		std::string error;
		{
			NEMTests::TestFileReadLock locked(second);
			if (Engine::JsonFileJournal::Commit(scope, changes, "Foundation", error, recover) || error.empty() ||
				Engine::StorageFileUtility::FileRevision(first) != before || Engine::JsonAdapter::Load(second) != original ||
				!Engine::JsonFileJournal::GetRecoveries(scope, true).empty()) return false;
		}
		if (!Engine::JsonFileJournal::Commit(scope, changes, "Foundation", error, recover) ||
			Engine::JsonAdapter::Load(first) != changes[0].data || Engine::JsonAdapter::Load(second) != changes[1].data) return false;
		// 準備失敗では本体と既存の復旧記録を維持する
		const auto records = Engine::JsonFileJournal::GetRecoveries(scope);
		const nlohmann::json invalid = {{ "text", std::string(1, static_cast<char>(0xff)) }};
		if (Engine::JsonFileJournal::Commit(scope, {{first, invalid}}, "Invalid", error, recover) ||
			Engine::JsonAdapter::Load(first) != changes[0].data || Engine::JsonFileJournal::GetRecoveries(scope) != records) return false;
		size_t directoryCount = 0;
		for (const auto& entry : std::filesystem::directory_iterator(scope.recoveryRoot)) {
			if (entry.is_directory()) ++directoryCount;
		}
		if (directoryCount != records.size()) return false;
		// 同じパスへの競合要求は何も保存せず拒否する
		return !Engine::JsonFileJournal::Commit(scope, {changes[0], changes[0]}, "Duplicate", error, recover);
	}

	// 表示文字列とpathの不正UTF処理を区別する
	bool TestTextConversion() {

		if (Engine::Algorithm::RemoveSubstring("abc", "") != "abc" ||
			Engine::Algorithm::RemoveSubstring("abab", "ab") != "") {
			return false;
		}
		// 不正な列の後にある正常文字を維持する
		if (Engine::Algorithm::Utf8ToCodepoints("\xe0" "A") != std::vector<char32_t>{ 0xfffd, U'A' } ||
			Engine::Algorithm::Utf8ToCodepoints("\xf0\x9f\x98\x80") != std::vector<char32_t>{ 0x1f600 } ||
			Engine::Algorithm::CodepointToUtf8(0x110000) != "\xef\xbf\xbd" ||
			Engine::Algorithm::CodepointToUtf8(0xd800) != "\xef\xbf\xbd") return false;
		try {
			Engine::Algorithm::PathFromUTF8(std::string("a\0b", 3));
			return false;
		} catch (const std::invalid_argument&) {
		}
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
		if (workers.Enqueue(1)) {
			return false;
		}
		workers.Start(2, [&sum](uint32_t&& value, uint32_t) { sum.fetch_add(value); });
		for (uint32_t i = 1; i <= 128; ++i) {
			workers.Enqueue(i);
		}
		workers.Stop();
		if (sum != 8256 || !workers.IsIdle() || workers.GetStats().threadCount != 0 || workers.Enqueue(1)) {
			return false;
		}
		workers.Start(1, [&sum](uint32_t&& value, uint32_t) { sum.fetch_add(value); });
		workers.Enqueue(7);
		workers.WaitIdle();
		const auto stats = workers.GetStats();
		if (sum != 8263 || stats.queuedCount != 0 || stats.inFlightCount != 0 || stats.threadCount != 1) {
			return false;
		}
		// 処理例外を受け取り、完了数と再起動を確認する
		workers.Start(1, [](uint32_t&&, uint32_t) { throw std::runtime_error("worker failure"); });
		workers.Enqueue(1);
		try {
			workers.WaitIdle();
			return false;
		} catch (const std::runtime_error&) {
			if (!workers.IsIdle() || workers.Enqueue(2)) {
				return false;
			}
		}
		workers.Start(1, [&sum](uint32_t&& value, uint32_t) { sum.fetch_add(value); });
		workers.Enqueue(1);
		workers.WaitIdle();
		if (sum != 8264) {
			return false;
		}
		// callback自身の完了待ちは停止せず例外として伝える
		workers.Start(1, [&workers](uint32_t&&, uint32_t) { workers.WaitIdle(); });
		workers.Enqueue(1);
		try {
			workers.WaitIdle();
			return false;
		} catch (const std::logic_error&) {
		}
		workers.Stop();
		// 別スレッドから開始と停止が重なっても所有を失わない
		std::thread starter([&workers]() {
			for (int i = 0; i < 8; ++i) workers.Start(1, [](uint32_t&&, uint32_t) {});
		});
		std::thread stopper([&workers]() {
			for (int i = 0; i < 8; ++i) workers.Stop();
		});
		starter.join();
		stopper.join();
		workers.Stop();
		return workers.IsIdle() && workers.GetStats().threadCount == 0;
	}

	// 終了後の出力でLoggerを再生成しない
	bool TestLoggerShutdown(const std::filesystem::path& root) {

		// Window終了通知はInputを生成しない
		if (Engine::Input::TryGetInstance()) return false;
		Engine::WindowInputBridge::NotifyFocus(false);
		Engine::WindowInputBridge::AppendCharacter(L'A');
		if (Engine::Input::TryGetInstance()) return false;
		Engine::Logger::Finalize();
		Engine::Logger::CreateLogFiles(root / "Logs");
		auto logger = Engine::Logger::Get(Engine::LogType::Engine);
		if (!logger) return false;
		Engine::Logger::BlankLine(Engine::LogType::Engine);
		Engine::Logger::Finalize();
		Engine::Logger::Output(Engine::LogType::Engine, "after shutdown");
		const bool stopped = !Engine::Logger::Get(Engine::LogType::Engine);
		// 借用中だったLoggerを解放してからfixtureを片付ける
		logger.reset();
		return stopped;
	}

	// descriptor探索とURI変換をglobal状態の更新なしで検証する
	bool TestResolvedPaths(const std::filesystem::path& root) {

		// 再取得しても保持済みのパス集合は変更されない
		const auto snapshot = Engine::RuntimePaths::GetSnapshot();
		const auto oldRoot = snapshot->gameRoot;
		const auto revision = snapshot->revision;
		Engine::RuntimePaths::Refresh();
		const auto current = Engine::RuntimePaths::GetSnapshot();
		if (snapshot == current || snapshot->revision != revision || snapshot->gameRoot != oldRoot ||
			current->revision <= revision) {
			return false;
		}
		// 未読のPackageには有効なhashを与えない
		if (Engine::PackageDetail::ComputePackageHash(root / "missing")) {
			return false;
		}
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
		NEMTests::TestDirectory directory("Foundation");
		const auto& root = directory.GetPath();
		const bool passed = TestMathContracts() && TestAffineDecomposition() && TestJsonStorage(root) && TestTextConversion() &&
			TestWorkerCompletion() && TestResolvedPaths(root) && TestJsonJournal(root) && TestLoggerShutdown(root);
		if (!passed) {
			std::cerr << "Foundation contracts failed\n";
		}
		return directory.Remove() && passed;
	} catch (const std::exception& error) {
		std::cerr << "Foundation contracts exception: " << error.what() << '\n';
		return false;
	}
}
