#pragma once

#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
#include <Engine/Core/World/ECS/Systems/Scheduler/SystemScheduler.h>

namespace NEMTests {

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
