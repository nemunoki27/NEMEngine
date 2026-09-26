#pragma once

#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
#include <Engine/Core/World/ECS/Systems/Scheduler/SystemScheduler.h>

// c++
#include <filesystem>
#include <string_view>
#include <vector>

namespace NEMTests {

	//============================================================================
	//	TestDirectory class
	//	検証専用ディレクトリを所有し終了時に片付ける
	//============================================================================
	class TestDirectory {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit TestDirectory(std::string_view name, const std::filesystem::path& parent = {});
		~TestDirectory();
		TestDirectory(const TestDirectory&) = delete;
		TestDirectory& operator=(const TestDirectory&) = delete;

		// 所有ディレクトリを削除する
		bool Remove();
		// Scene削除前に外部Actorの所有を保持する
		void CaptureSceneAssets();
		std::vector<std::filesystem::path> GetSceneRecoveries() const;

		//--------- accessor -----------------------------------------------------

		const std::filesystem::path& GetPath() const { return path_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// この検証だけが所有する作業先
		std::filesystem::path path_;
		std::filesystem::path recoveryRoot_;
		std::vector<std::filesystem::path> actorRoots_;
	};

	// 読込は許可し、置換と書込を失敗させる
	class TestFileReadLock {
	public:
		explicit TestFileReadLock(const std::filesystem::path& path);
		~TestFileReadLock();
		TestFileReadLock(const TestFileReadLock&) = delete;
		TestFileReadLock& operator=(const TestFileReadLock&) = delete;
	private:
		void* handle_ = nullptr;
	};

	struct TestEnableableComponent {

		static constexpr bool kEnableable = true;

		int32_t value = 0;
	};

	struct TestBufferElement {

		static constexpr Engine::ComponentStorageKind kStorageKind =
			Engine::ComponentStorageKind::Buffer;

		int32_t value = 0;
	};

	struct TestBlobRoot {

		uint32_t id = 0;
		Engine::BlobArray<int32_t> values{};
	};

	class SceneContextObserverSystem final :
		public Engine::ISystem {
	public:
		void OnSceneInstancesChanged(Engine::ECSWorld& world, Engine::SystemContext& context,
			Engine::SceneChangePhase phase) override;

		const char* GetName() const override { return "SceneContextObserverSystem"; }

		const Engine::SceneHeader* observedHeader = nullptr;
		uint32_t notificationCount = 0;
	};

	void to_json(nlohmann::json& out, const TestEnableableComponent& component);
	void from_json(const nlohmann::json& in, TestEnableableComponent& component);
	void to_json(nlohmann::json& out, const TestBufferElement& element);
	void from_json(const nlohmann::json& in, TestBufferElement& element);
	void RegisterTestComponents();
}
