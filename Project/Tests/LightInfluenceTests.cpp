#include "LightInfluenceTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Lighting/PointLightComponent.h>
#include <Engine/Core/World/Components/Lighting/SpotLightComponent.h>
#include <Engine/Core/World/Components/Lighting/RectLightComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.h>
#include <Engine/Core/Scripting/Managed/Generated/ManagedComponentBindings.generated.h>
#include <Engine/Core/Scripting/Managed/ManagedWorldRegistry.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptUtility.h>
#include <Engine/Core/Rendering/Renderer/Lighting/GPU/LightGPUConversion.h>
#include <Engine/Core/Rendering/DxObject/Core/DxShaderCompiler.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>
#include <Engine/Core/World/Behavior/MonoBehavior.h>
#include <Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <algorithm>
#include <array>
#include <iostream>

namespace {

	// 旧データの初期値と保存参照の再読込を照合する
	template <typename T>
	bool CheckLightSerialization() {

		T light = nlohmann::json::object().get<T>();
		if (light.influence.renderingLayerMask != 0x00FFFFFFu || light.influence.shadowExcludedLayerMask != 0u ||
			!light.influence.shadowExclusions.empty()) return false;
		light.influence.renderingLayerMask = 3u;
		light.influence.shadowExcludedLayerMask = 2u;
		Engine::LightShadowExclusion exclusion;
		exclusion.target.localFileID = Engine::FromString16Hex("1234567890abcdef");
		exclusion.includeChildren = true;
		light.influence.shadowExclusions.push_back(exclusion);
		nlohmann::json saved = light;
		const auto local = Engine::FromString16Hex("abcdef1234567890");
		Engine::PrefabReferenceRemapper::RemapComponent("PointLight", saved,
			{{exclusion.target.localFileID, local}}, Engine::PrefabReferenceRemapper::ReferenceSpace::Prefab, {});
		const T restored = saved.get<T>();
		return restored.influence.renderingLayerMask == 3u && restored.influence.shadowExcludedLayerMask == 2u &&
			restored.influence.shadowExclusions.size() == 1u && restored.influence.shadowExclusions[0].includeChildren &&
			restored.influence.shadowExclusions[0].target.localFileID == local;
	}
}

bool NEMTests::TestLightInfluence() {

	using namespace Engine;
	static_assert(sizeof(DirectionalLightGPU) == 48 && sizeof(PointLightGPU) == 64 &&
		sizeof(SpotLightGPU) == 80 && sizeof(RectLightGPU) == 112);
	if (!CheckLightSerialization<DirectionalLightComponent>() || !CheckLightSerialization<PointLightComponent>() ||
		!CheckLightSerialization<SpotLightComponent>() || !CheckLightSerialization<RectLightComponent>()) return false;
	ECSWorld world;
	const auto owner = SceneAuthoring::CreateGameObject(world, "Light");
	const auto root = SceneAuthoring::CreateGameObject(world, "Emitter");
	const auto child = SceneAuthoring::CreateGameObject(world, "Part");
	const auto wall = SceneAuthoring::CreateGameObject(world, "Wall");
	HierarchySystem hierarchy;
	hierarchy.SetParent(world, child, root);
	LightInfluenceSettings settings;
	settings.shadowExclusions.push_back({{}, false, world.GetUUID(root)});
	auto exclusions = ResolveLightShadowExclusions(world, owner, settings);
	if (exclusions.size() != 1 || !IsLightShadowExcluded(settings, exclusions, root, 1u) ||
		IsLightShadowExcluded(settings, exclusions, child, 1u) || IsLightShadowExcluded(settings, exclusions, wall, 1u)) return false;
	// 同じ対象の後続指定が子孫を含めても取りこぼさない
	settings.shadowExclusions.push_back({{}, true, world.GetUUID(root)});
	exclusions = ResolveLightShadowExclusions(world, owner, settings);
	if (exclusions.size() != 2 || !IsLightShadowExcluded(settings, exclusions, child, 1u)) return false;
	settings.shadowExcludedLayerMask = 2u;
	if (!IsLightShadowExcluded(settings, exclusions, wall, 3u) || IsLightShadowExcluded(settings, exclusions, wall, 1u)) return false;
	ECSWorld other;
	SceneAuthoring::CreateGameObject(other, "Other");
	if (!ResolveLightShadowExclusions(other, {}, settings).empty()) return false;
	PointLightItem point;
	point.common.influence.renderingLayerMask = 2u;
	if (LightGPUConversion::ToGPU(point).renderingLayerMask != 2u) return false;
	// 全ライトの生成Bindingから同じ設定へ書き込める
	world.AddComponent<DirectionalLightComponent>(owner);
	world.AddComponent<PointLightComponent>(owner);
	world.AddComponent<SpotLightComponent>(owner);
	world.AddComponent<RectLightComponent>(owner);
	auto& registry = ManagedWorldRegistry::GetInstance();
	const auto handle = registry.Register(world);
	const auto native = MakeNativeEntity(world, owner);
	const std::array<std::pair<int32_t, int32_t>, 4> properties{{{7, 8}, {18, 9}, {28, 12}, {59, 11}}};
	bool valid = true;
	for (const auto [type, property] : properties) {
		const uint32_t mask = 3u;
		valid &= GeneratedComponentBindings::SetComponentProperty(native, type, property, &mask, sizeof(mask)) == ManagedStatus::Ok;
	}
	registry.Unregister(handle);
	return valid && world.GetComponent<DirectionalLightComponent>(owner).influence.renderingLayerMask == 3u &&
		world.GetComponent<PointLightComponent>(owner).influence.renderingLayerMask == 3u &&
		world.GetComponent<SpotLightComponent>(owner).influence.renderingLayerMask == 3u &&
		world.GetComponent<RectLightComponent>(owner).influence.renderingLayerMask == 3u;
}

bool NEMTests::TestLightInfluenceShaders() {

	using namespace Engine;
	struct ShaderCase {
		const char* file;
		const wchar_t* profile;
		const wchar_t* entry;
		ShaderStage stage;
	};
	const std::array cases{
		ShaderCase{"Lighting/deferredLighting.PS.hlsl", L"ps_6_6", L"main", ShaderStage::PS},
		ShaderCase{"Lighting/inlineShadowTrace.CS.hlsl", L"cs_6_6", L"main", ShaderStage::CS},
		ShaderCase{"Primitive/primitive.PS.hlsl", L"ps_6_6", L"mainTransparent", ShaderStage::PS},
		ShaderCase{"Particle/GPU/particleGPUDraw.PS.hlsl", L"ps_6_6", L"main", ShaderStage::PS},
		ShaderCase{"Raytracing/reflection.RT.hlsl", L"lib_6_6", L"", ShaderStage::Lib}
	};
	DxShaderCompiler compiler;
	compiler.Init();
	for (const auto& shader : cases) {
		const auto path = RuntimePaths::GetEngineAssetPath(std::string("Rendering/Builtin/") + shader.file);
		if (!compiler.CompileShader(path.wstring(), shader.profile, shader.entry, shader.stage).IsValid()) {
			std::cerr << "Light shader compilation failed: " << shader.file << '\n';
			return false;
		}
	}
	return true;
}

bool NEMTests::TestLightInfluenceManaged() {

	using namespace Engine;
	try {
		TestDirectory directory("LightInfluence");
		const auto root = directory.GetPath();
		std::filesystem::create_directories(root / "GameAssets");
		if (!JsonFile::Save(root / "Light.nemproject", {{"schemaVersion", 1},
			{"projectGuid", "2b940d5c54f44dd0b9791784c44278e1"}, {"name", "Light"}, {"sceneStorage", "Monolithic"},
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
		const auto* type = BehaviorTypeRegistry::GetInstance().FindByName("NEMTests.Scripts.LightInfluenceAPIProbe");
		if (!type) return false;
		ECSWorld world(ECSWorldKind::Runtime);
		const auto handle = ManagedWorldRegistry::GetInstance().Register(world);
		ScopedCleanup unregister([&]() noexcept { ManagedWorldRegistry::GetInstance().Unregister(handle); });
		const auto owner = SceneAuthoring::CreateGameObject(world, "Lamp");
		SceneAuthoring::CreateGameObject(world, "Emitter");
		SystemContext context;
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
		const auto error = state.value("82c446a6-392c-449a-a8a3-c69e0bf587b4", std::string{});
		if (!error.empty()) std::cerr << error << '\n';
		return error.empty() && state.value("64c04024-a95f-46a5-b3ec-880a669f1a80", false);
	} catch (const std::exception& error) {
		std::cerr << "Light managed API: " << error.what() << '\n';
		return false;
	}
}
