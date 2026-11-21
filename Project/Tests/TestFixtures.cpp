#include "TestFixtures.h"

#include "ApplicationPlatformTests.h"

//============================================================================
//	include
//============================================================================
#include "FoundationTests.h"
#include "EditorRefactoringTests.h"
#include "GameplayRefactoringTests.h"
#include "SceneStorageTests.h"
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Core/Scripting/Managed/ManagedWorldRegistry.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Behavior/MonoBehavior.h>
#include <Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Assets/Database/AssetDocumentRecovery.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
#include <Engine/Core/World/ECS/Systems/Scheduler/SystemScheduler.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>

// Windows
#include <Windows.h>

namespace NEMTests {

	TestDirectory::TestDirectory(std::string_view name, const std::filesystem::path& parent) {

		hadActorDirectory_ = std::filesystem::is_directory(Engine::RuntimePaths::GetGameAssetsRoot() / "ExternalActors");
		// 他の検証やゲームAssetと作業先を分ける
		const auto root =
			parent.empty() ? Engine::RuntimePaths::GetEngineProjectRoot().parent_path() / "LocalWork/Tests/TestFixtures" : parent;
		std::filesystem::create_directories(root);
		for (int attempt = 0; attempt < 8; ++attempt) {
			const auto candidate =
				root / Engine::Algorithm::PathFromUTF8(std::string(name) + "_" + Engine::ToString(Engine::UUID::New()));
			if (std::filesystem::create_directory(candidate)) {
				path_ = candidate;
				return;
			}
		}
		throw std::runtime_error("Test directory creation failed");
	}

	TestDirectory::~TestDirectory() {

		// 途中returnや例外でも所有ファイルを片付ける
		if (!Remove()) {
			std::cerr << "Test directory cleanup failed: " << path_ << '\n';
		}
	}

	bool TestDirectory::Remove() {

		// 自分が作成したディレクトリだけを削除する
		try {
			CaptureSceneAssets();
			const auto recoveries = GetStorageRecoveries();
			std::filesystem::remove_all(path_);
			for (const auto& root : actorRoots_) {
				std::filesystem::remove_all(root);
			}
			if (!hadActorDirectory_ && !actorRoots_.empty()) {
				std::error_code ec;
				const auto parent = actorRoots_.front().parent_path();
				if (std::filesystem::is_directory(parent, ec)) std::filesystem::remove(parent, ec);
			}
			for (const auto& recovery : recoveries) {
				std::filesystem::remove_all(recovery);
			}
			return true;
		} catch (const std::exception& error) {
			std::cerr << "Test cleanup: " << error.what() << '\n';
			return false;
		}
	}

	void TestDirectory::CaptureSceneAssets() {

		if (!std::filesystem::exists(path_)) {
			return;
		}
		// 検証用Sceneのmetaが所有するActorだけを回収する
		for (const auto& entry : std::filesystem::recursive_directory_iterator(path_)) {
			if (!entry.is_regular_file() || entry.path().extension() != ".meta") {
				continue;
			}
			Engine::AssetMeta meta;
			if (!Engine::AssetDatabase::ReadMetaFile(entry.path(), meta) || !meta.guid ||
				meta.type != Engine::AssetType::Scene) {
				continue;
			}
			const auto actors = Engine::RuntimePaths::GetGameAssetsRoot() / "ExternalActors" / Engine::ToString(meta.guid);
			if (std::find(actorRoots_.begin(), actorRoots_.end(), actors) == actorRoots_.end()) {
				actorRoots_.push_back(actors);
			}
		}
	}

	std::vector<std::filesystem::path> TestDirectory::GetStorageRecoveries() const {

		std::vector<std::filesystem::path> result;
		std::vector<std::filesystem::path> roots{Engine::RuntimePaths::GetSavedRoot() / "SceneAssetRecovery"};
		for (const auto kind : Engine::AssetDocumentRecovery::GetSaveKinds()) {
			roots.push_back(Engine::AssetDocumentRecovery::MakeScope(kind).recoveryRoot);
		}
		for (const auto& root : roots) {
			if (!std::filesystem::exists(root)) {
				continue;
			}
			for (const auto& entry : std::filesystem::directory_iterator(root)) {
				if (!entry.is_directory()) {
					continue;
				}
				auto record = Engine::JsonAdapter::Load(entry.path() / "operation.json", false);
				if (!record.is_object()) {
					record = Engine::JsonAdapter::Load(entry.path() / "operation.json.bak", false);
				}
				if (!record.is_object()) {
					record = Engine::JsonAdapter::Load(entry.path() / "operation.json.tmp", false);
				}
				if (!record.is_object() || !record.contains("files") || !record["files"].is_array() ||
					record["files"].empty()) {
					continue;
				}
				// 他の操作と混在した記録は削除しない
				const bool owned = std::all_of(record["files"].begin(), record["files"].end(), [&](const auto& file) {
					if (!file.is_object() || !file.contains("path") || !file["path"].is_string()) {
						return false;
					}
					const auto target = Engine::Algorithm::PathFromUTF8(file["path"].template get<std::string>());
					return Engine::StorageFileUtility::IsInside(target, path_) ||
						   std::any_of(actorRoots_.begin(), actorRoots_.end(),
							   [&](const auto& root) { return Engine::StorageFileUtility::IsInside(target, root); });
				});
				if (owned) {
					result.push_back(entry.path());
				}
			}
		}
		return result;
	}

	TestFileReadLock::TestFileReadLock(const std::filesystem::path& path, bool allowRead) {

		handle_ = CreateFileW(path.c_str(), GENERIC_READ, allowRead ? FILE_SHARE_READ : 0, nullptr, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (handle_ == INVALID_HANDLE_VALUE) {
			throw std::runtime_error("Test file lock failed");
		}
	}

	TestFileReadLock::~TestFileReadLock() {

		CloseHandle(handle_);
	}

	void SceneContextObserverSystem::OnSceneInstancesChanged([[maybe_unused]] Engine::ECSWorld& world,
		Engine::SystemContext& context, [[maybe_unused]] Engine::SceneChangePhase phase) {

		observedHeader = context.GetActiveSceneHeader();
		++notificationCount;
	}

	void to_json(nlohmann::json& out, const TestEnableableComponent& component) {

		out = component.value;
	}

	void from_json(const nlohmann::json& in, TestEnableableComponent& component) {

		component.value = in.get<int32_t>();
	}

	void to_json(nlohmann::json& out, const TestBufferElement& element) {

		out = element.value;
	}

	void from_json(const nlohmann::json& in, TestBufferElement& element) {

		element.value = in.get<int32_t>();
	}

	void RegisterTestComponents() {

		[[maybe_unused]] static const bool registered = [] {
			Engine::ComponentTypeRegistry& registry = Engine::ComponentTypeRegistry::GetInstance();
			registry.Register<TestEnableableComponent>(registry.GetComponentTypeCount(), "TestEnableable");
			registry.Register<TestBufferElement>(registry.GetComponentTypeCount(), "TestBuffer");
			return true;
		}();
	}

}

bool NEMTests::RunManagedComponentProbe(const char* name, const char* typeName, const char* passedField, const char* errorField,
	void (*setup)(Engine::ECSWorld&), bool (*setupAssets)(Engine::AssetDatabase&)) {

	using namespace Engine;
	try {
		TestDirectory directory(name);
		const auto root = directory.GetPath();
		std::filesystem::create_directories(root / "GameAssets");
		if (!JsonFile::Save(root / "Probe.nemproject", {{"schemaVersion", 1},
			{"projectGuid", "2b940d5c54f44dd0b9791784c44278e1"}, {"name", name}, {"sceneStorage", "Monolithic"},
			{"assetsDirectory", "GameAssets"}, {"packagesDirectory", "Packages"},
			{"projectSettingsDirectory", "ProjectSettings"}})) return false;
		const auto previous = std::filesystem::current_path();
		ScopedCleanup restore([&]() noexcept {
			std::error_code error;
			std::filesystem::current_path(previous, error);
			try { RuntimePaths::Refresh(); } catch (...) { }
		});
		const auto previousLogger = spdlog::default_logger();
		const bool ownsLogger = !Logger::Get(LogType::Engine);
		if (ownsLogger) Logger::CreateLogFiles(root / "Logs");
		ScopedCleanup closeLog([&]() noexcept {
			if (ownsLogger) { Logger::Finalize(); spdlog::set_default_logger(previousLogger); }
		});
		std::filesystem::current_path(root);
		RuntimePaths::Refresh();
		auto& runtime = ManagedScriptRuntime::GetInstance();
		if (!runtime.Init()) return false;
		ScopedCleanup finalize([&]() noexcept { runtime.Finalize(); });
		const auto assembly = Algorithm::GetExecutablePath().parent_path() / "ManagedFixtures/GameScripts.dll";
		if (!runtime.LoadGameAssemblyFromPath(assembly, false)) return false;
		const auto* type = BehaviorTypeRegistry::GetInstance().FindByName(typeName);
		if (!type) return false;
		ECSWorld world(ECSWorldKind::Runtime);
		const auto handle = ManagedWorldRegistry::GetInstance().Register(world);
		ScopedCleanup unregister([&]() noexcept { ManagedWorldRegistry::GetInstance().Unregister(handle); });
		const auto owner = SceneAuthoring::CreateGameObject(world, "Probe");
		setup(world);
		AssetDatabase database;
		RenderAssetLibrary library;
		SystemContext context;
		if (setupAssets) {
			if (!database.Init() || !setupAssets(database)) return false;
			library.Init(&database);
			context.assetDatabase = &database;
			context.renderAssetLibrary = &library;
		}
		context.world = &world;
		context.mode = WorldMode::Play;
		BehaviorSystem behaviors;
		behaviors.OnWorldEnter(world, context);
		ScopedCleanup exit([&]() noexcept { try { behaviors.OnWorldExit(world, context); } catch (...) { } });
		auto* probe = BehaviorSystem::AttachScript(owner, type->scriptTypeID, context);
		if (!probe) return false;
		behaviors.Update(world, context);
		behaviors.Update(world, context);
		const auto state = probe->GetRuntimeSerializedState();
		const auto error = state.value(errorField, std::string{});
		if (!error.empty()) std::cerr << error << '\n';
		return error.empty() && state.value(passedField, false);
	} catch (const std::exception& error) {
		std::cerr << name << " managed API: " << error.what() << '\n';
		return false;
	}
}
