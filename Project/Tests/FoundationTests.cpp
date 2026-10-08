#include "FoundationTests.h"
#include "TestFixtures.h"
#include "ScopedValueTests.h"
#include "FoundationMathTests.h"
#include "FoundationJournalTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Async/AssetWorkerPool.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/Algorithm/EnvironmentUtility.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Utility/Enum/Axis.h>
#include <Engine/Core/Runtime/Paths/RuntimeAssetPaths.h>
#include <Engine/Core/Runtime/Packages/PackageResolution.h>
#include <Engine/Core/Platform/Input/InputSystem.h>
#include <Engine/Core/Platform/Windows/WindowInputBridge.h>
#include <Engine/Core/Runtime/Paths/RuntimePathResolution.h>
#include <Engine/Core/Scripting/Managed/ManagedBuildUtility.h>

// c++
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <vector>
// windows
#include <Windows.h>

namespace {

	// 連想と順序コンテナで同じ探索契約を確認する
	bool TestContainerSearch() {

		const std::unordered_map<int, int> mapping{{1, 10}, {3, 30}};
		const std::vector<int> sequence{1, 3, 5};
		return Engine::Algorithm::Find(mapping, 3) && !Engine::Algorithm::Find(mapping, 2) &&
			   Engine::Algorithm::Find(sequence, 5) && !Engine::Algorithm::Find(sequence, 4) &&
			   !Engine::Algorithm::Find(std::vector<int>{}, 1);
	}

	// CRTの取得後もOS側の変更を反映するか確認
	bool TestEnvironmentPath() {

		using Engine::Algorithm::TryReadProcessEnvironment;
		constexpr const wchar_t* name = L"NEMENGINE_TEST_ENVIRONMENT_PATH";
		std::wstring previous;
		bool hadPrevious = false;
		if (!TryReadProcessEnvironment(name, previous, hadPrevious)) return false;
		Engine::ScopedCleanup restore([&]() noexcept {
			::SetEnvironmentVariableW(name, hadPrevious ? previous.c_str() : nullptr);
		});
		std::wstring value = L"保持";
		bool exists = true;
		// 不正な名前の失敗時は出力を変更しない
		if (TryReadProcessEnvironment(L"", value, exists) ||
			TryReadProcessEnvironment(std::wstring(L"INVALID\0NAME", 12), value, exists) ||
			TryReadProcessEnvironment(L"INVALID=NAME", value, exists) || value != L"保持" || !exists) return false;
		const std::filesystem::path expected(L"C:/日本語/Program Files/Editor");
		if (!::SetEnvironmentVariableW(name, expected.c_str()) ||
			Engine::Algorithm::GetEnvironmentPath(name) != expected) return false;
		// CRTで取得した後にOS側だけで値を変更
		wchar_t* cached = nullptr;
		size_t cachedLength = 0;
		_wdupenv_s(&cached, &cachedLength, name);
		std::free(cached);
		const std::wstring longValue(8192, L'値');
		if (!::SetEnvironmentVariableW(name, longValue.c_str()) ||
			!TryReadProcessEnvironment(name, value, exists) || !exists || value != longValue) return false;
		// 空値と削除後の未設定を区別
		if (!::SetEnvironmentVariableW(name, L"") ||
			!TryReadProcessEnvironment(name, value, exists) || !exists || !value.empty()) return false;
		return ::SetEnvironmentVariableW(name, nullptr) && TryReadProcessEnvironment(name, value, exists) &&
			!exists && value.empty() && Engine::Algorithm::GetEnvironmentPath(name).empty();
	}

	// 配置元の探索とRuntimeの正規化が同じEXEを指すか
	bool TestExecutablePath() {

		const auto executable = Engine::Algorithm::GetExecutablePath();
		std::error_code error;
		return executable.is_absolute() && std::filesystem::is_regular_file(executable, error) && !error &&
			Engine::RuntimePathDetail::GetExecutablePath() == Engine::RuntimePathDetail::NormalizePath(executable);
	}

	// 列挙名の配列を定数評価でも取得できるか確認する
	bool TestEnumConversion() {

		using Adapter = Engine::EnumAdapter<Engine::Axis>;
		constexpr auto names = Adapter::GetEnumArray();
		static_assert(names.size() == 3 && names[0][0] == 'X' && names[2][0] == 'Z');
		static_assert(Adapter::GetValue(1) == Engine::Axis::Y);
		static_assert(Adapter::GetIndex(Engine::Axis::Z) == 2);
		return Adapter::FromString("Y") == Engine::Axis::Y && !Adapter::FromString("invalid") &&
			   std::string_view(Adapter::GetEnumName(3)).empty() && Adapter::ToStringView(Engine::Axis::X) == "X" &&
			   Engine::GetDirection({}) == Engine::Vector3{} &&
			   Engine::GetDirection({Engine::Axis::Y}) == Engine::Vector3{0, 1, 0};
	}

	// MSBuildへ渡すDirectory propertyの形式を確認する
	bool TestMSBuildDirectory() {

		using Engine::ManagedBuildUtility::ToMSBuildDirectory;
		return ToMSBuildDirectory(LR"(C:\Build Root\obj)") == LR"(C:/Build Root/obj/)" &&
			   ToMSBuildDirectory(LR"(C:\Build Root\obj\)") == LR"(C:/Build Root/obj/)" && ToMSBuildDirectory({}).empty();
	}

	// 保存byteと失敗時の既存ファイル保持を確認する
	bool TestJsonStorage(const std::filesystem::path& root) {

		const auto filePath = root / Engine::Algorithm::PathFromUTF8("保存.json");
		const nlohmann::json data = {{"z", -0.0}, {"a", {1, 2, 3}}};
		if (!Engine::JsonAdapter::SaveCanonical(filePath, data, 2)) {
			return false;
		}
		std::ifstream input(filePath, std::ios::binary);
		std::stringstream contents;
		contents << input.rdbuf();
		input.close();
		const auto savedTime = std::filesystem::last_write_time(filePath);
		if (contents.str() != "{\n  \"a\": [\n    1,\n    2,\n    3\n  ],\n  \"z\": 0.0\n}\n" ||
			!Engine::JsonAdapter::SaveCanonical(filePath, data, 2) || std::filesystem::last_write_time(filePath) != savedTime) {
			return false;
		}
		const nlohmann::json invalid = {{"value", std::numeric_limits<double>::infinity()}};
		if (Engine::JsonAdapter::SaveCanonical(filePath, invalid) || Engine::JsonAdapter::Load(filePath) != data ||
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
		const nlohmann::json invalidText = {{"text", std::string("\xff")}};
		if (Engine::JsonAdapter::SaveCanonical(filePath, invalidText) || Engine::JsonAdapter::Load(filePath) != data) {
			return false;
		}
		nlohmann::json values;
		Engine::JsonAdapter::SetVector3(values, "position", {1.0f, 2.0f, 3.0f});
		const auto position = Engine::JsonAdapter::GetVector3(values, "position");
		values["position"]["z"] = "invalid";
		const auto fallback = Engine::JsonAdapter::GetVector3(values, "position", {4.0f, 5.0f, 6.0f});
		return position.x == 1.0f && position.y == 2.0f && position.z == 3.0f && fallback.x == 4.0f && fallback.y == 5.0f &&
			   fallback.z == 6.0f;
	}

	// バイナリ公開と失敗時の作業ファイル回収を確認する
	bool TestAtomicByteStorage(const std::filesystem::path& root) {

		const auto directory = root / "AtomicBytes";
		std::filesystem::create_directory(directory);
		const auto target = directory / Engine::Algorithm::PathFromUTF8("保存.bin");
		const auto existingTemporary = directory / "unrelated.tmp";
		std::ofstream(existingTemporary) << "external";
		const std::string original("a\0b", 3);
		if (!Engine::StorageFileUtility::WriteBytes(target, original)) {
			return false;
		}
		const auto before = Engine::StorageFileUtility::FileRevision(target);
		const auto savedTime = std::filesystem::last_write_time(target);
		// 同じ内容の保存ではファイルの更新を省く
		if (!Engine::StorageFileUtility::WriteBytes(target, original) ||
			std::filesystem::last_write_time(target) != savedTime) {
			return false;
		}
		// 保存先の使用中は元の内容と外部ファイルを維持する
		{
			NEMTests::TestFileReadLock lock(target);
			if (Engine::StorageFileUtility::WriteBytes(target, "changed") ||
				Engine::StorageFileUtility::FileRevision(target) != before) {
				return false;
			}
		}
		for (const auto& entry : std::filesystem::directory_iterator(directory)) {
			if (entry.path() != target && entry.path() != existingTemporary) {
				return false;
			}
		}
		// 解放後はバイナリと空ファイルを公開できる
		const std::string replacement(1024 * 1024, '\0');
		if (!Engine::StorageFileUtility::WriteBytes(target, replacement) ||
			std::filesystem::file_size(target) != replacement.size() || !Engine::StorageFileUtility::WriteBytes(target, "") ||
			std::filesystem::file_size(target) != 0) {
			return false;
		}
		// フォルダーへ公開できなくても作業ファイルを残さない
		const auto blocked = directory / "blocked";
		std::filesystem::create_directory(blocked);
		if (Engine::StorageFileUtility::WriteBytes(blocked, "changed") || !std::filesystem::is_directory(blocked)) {
			return false;
		}
		for (const auto& entry : std::filesystem::directory_iterator(directory)) {
			if (entry.path() != target && entry.path() != existingTemporary && entry.path() != blocked) {
				return false;
			}
		}
		std::ifstream retained(existingTemporary);
		std::string contents;
		retained >> contents;
		return contents == "external";
	}

	// 表示文字列とpathの不正UTF処理を区別する
	bool TestTextConversion() {

		if (Engine::Algorithm::RemoveSubstring("abc", "") != "abc" || Engine::Algorithm::RemoveSubstring("abab", "ab") != "") {
			return false;
		}
		// 不正な列の後にある正常文字を維持する
		if (Engine::Algorithm::UTF8ToCodepoints("\xe0"
												"A") != std::vector<char32_t>{0xfffd, U'A'} ||
			Engine::Algorithm::UTF8ToCodepoints("\xf0\x9f\x98\x80") != std::vector<char32_t>{0x1f600} ||
			Engine::Algorithm::CodepointToUTF8(0x110000) != "\xef\xbf\xbd" ||
			Engine::Algorithm::CodepointToUTF8(0xd800) != "\xef\xbf\xbd") {
			return false;
		}
		// 表示と厳密変換で不正文字の扱いを区別する
		const std::string invalid = "\xff"
									"A";
		if (Engine::Algorithm::ConvertString(invalid) != std::wstring{wchar_t{0xfffd}, L'A'} ||
			Engine::Algorithm::ConvertStringStrict(std::string("a\0b", 3)) != std::wstring(L"a\0b", 3)) {
			return false;
		}
		try {
			Engine::Algorithm::ConvertStringStrict(invalid);
			return false;
		} catch (const std::runtime_error&) {
		}
		try {
			Engine::Algorithm::ConvertString(std::wstring(1, wchar_t{0xd800}));
			return false;
		} catch (const std::runtime_error&) {
		}
		try {
			Engine::Algorithm::PathFromUTF8(std::string("a\0b", 3));
			return false;
		} catch (const std::invalid_argument&) {
		}
		const std::string text = "日本語/素材.png";
		if (Engine::Algorithm::PathToUTF8(Engine::Algorithm::PathFromUTF8(text)) != text ||
			Engine::Algorithm::ConvertString(Engine::Algorithm::ConvertString(text)) != text ||
			Engine::Algorithm::UTF8ToCodepoints("\xff") != std::vector<char32_t>{0xfffd}) {
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
			for (int i = 0; i < 8; ++i) {
				workers.Start(1, [](uint32_t&&, uint32_t) {});
			}
		});
		std::thread stopper([&workers]() {
			for (int i = 0; i < 8; ++i) {
				workers.Stop();
			}
		});
		starter.join();
		stopper.join();
		workers.Stop();
		return workers.IsIdle() && workers.GetStats().threadCount == 0;
	}

	// 終了後の出力でLoggerを再生成しない
	bool TestLoggerShutdown(const std::filesystem::path& root) {

		// Window終了通知はInputを生成しない
		if (Engine::Input::TryGetInstance()) {
			return false;
		}
		Engine::WindowInputBridge::NotifyFocus(false);
		Engine::WindowInputBridge::AppendCharacter(L'A');
		if (Engine::Input::TryGetInstance()) {
			return false;
		}
		Engine::Logger::Finalize();
		Engine::Logger::CreateLogFiles(root / "Logs");
		auto logger = Engine::Logger::Get(Engine::LogType::Engine);
		if (!logger) {
			return false;
		}
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
		state.packages.push_back({"com.test", "1", "embedded", root / "Package", 0});
		const auto resolved = Engine::RuntimePathDetail::ResolveVirtualPath(state, "game://Scenes/test.scene.json");
		return resolved == state.gameAssetsRoot / "Scenes/test.scene.json" &&
			   Engine::RuntimePathDetail::ToAssetPath(state, resolved) == "GameAssets/Scenes/test.scene.json" &&
			   Engine::RuntimePathDetail::ResolveVirtualPath(state, "game://../escape").empty() &&
			   Engine::RuntimePathDetail::ResolveVirtualPath(state, "package://com.test/data.json") ==
				   root / "Package/data.json" &&
			   Engine::RuntimePathDetail::ResolveVirtualPath(state, "package://missing/data.json").empty();
	}
}

bool TestFoundationContracts() {

	try {
		NEMTests::TestDirectory directory("Foundation");
		const auto& root = directory.GetPath();
		const bool passed =
			NEMTests::TestMathContracts() && TestEnvironmentPath() && TestExecutablePath() &&
			TestContainerSearch() && TestEnumConversion() && TestMSBuildDirectory() &&
			NEMTests::TestAffineDecomposition() && NEMTests::TestScopedValueContracts() && TestJsonStorage(root) &&
			TestAtomicByteStorage(root) && TestTextConversion() && TestWorkerCompletion() && TestResolvedPaths(root) &&
			NEMTests::TestJsonJournal(root) && NEMTests::TestJsonJournalCreation(root) && TestLoggerShutdown(root);
		if (!passed) {
			std::cerr << "Foundation contracts failed\n";
		}
		return directory.Remove() && passed;
	} catch (const std::exception& error) {
		std::cerr << "Foundation contracts exception: " << error.what() << '\n';
		return false;
	}
}
