#include "TestFixtures.h"

#include "ApplicationPlatformTests.h"

//============================================================================
//	include
//============================================================================
#include "FoundationTests.h"
#include "EditorRefactoringTests.h"
#include "GameplayRefactoringTests.h"
#include "SceneStorageTests.h"
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
#include <Engine/Core/World/ECS/Systems/Scheduler/SystemScheduler.h>

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

namespace NEMTests {

	void SceneContextObserverSystem::OnSceneInstancesChanged([[maybe_unused]] Engine::ECSWorld& world,
		Engine::SystemContext& context, [[maybe_unused]] Engine::SceneChangePhase phase) {

		observedHeader = context.activeSceneHeader;
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

			Engine::ComponentTypeRegistry& registry =
				Engine::ComponentTypeRegistry::GetInstance();
			registry.Register<TestEnableableComponent>(
				registry.GetComponentTypeCount(), "TestEnableable");
			registry.Register<TestBufferElement>(
				registry.GetComponentTypeCount(), "TestBuffer");
			return true;
			}();
	}

}
