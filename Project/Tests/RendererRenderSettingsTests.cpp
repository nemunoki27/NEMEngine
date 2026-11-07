#include "RendererRenderSettingsTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/RendererRenderSettings.h>
#include <Engine/Core/Scripting/Managed/ManagedRendererSettings.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/LineRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>

namespace {

	using namespace Engine;

	template<class T>
	bool CheckComponent(int32_t type) {

		T component;
		if (!component.renderSettings.inheritMaterial) return false;
		const auto legacy = component.renderSettings;
		nlohmann::json saved = component;
		saved.erase("renderSettings");
		auto loaded = saved.get<T>();
		if (loaded.renderSettings != legacy) return false;
		component.renderSettings.inheritMaterial = false;
		component.renderSettings.fillMode = RendererFillMode::WireFrame;
		component.renderSettings.depthTest = false;
		component.renderSettings.depthWrite = true;
		component.renderSettings.stencilEnabled = true;
		component.renderSettings.separateBackFace = true;
		component.renderSettings.stencilReference = 237;
		component.renderSettings.stencilReadMask = 170;
		component.renderSettings.stencilWriteMask = 85;
		component.renderSettings.frontFace.pass = RendererStencilOperation::REPLACE;
		component.renderSettings.backFace.comparison = RendererDepthComparison::NOT_EQUAL;
		component.renderSettings.backFace.depthFail = RendererStencilOperation::DECR;
		saved = component;
		loaded = saved.get<T>();
		if (loaded.renderSettings != component.renderSettings) return false;
		ECSWorld world(ECSWorldKind::Runtime);
		const auto entity = SceneAuthoring::CreateGameObject(world, "RendererSettings");
		world.AddComponent<T>(entity);
		auto* settings = ResolveRendererSettings(world, entity, type);
		if (!settings || *settings != legacy) return false;
		*settings = loaded.renderSettings;
		return world.TryGetComponent<T>(entity)->renderSettings == component.renderSettings;
	}
}

bool NEMTests::TestRendererRenderSettings() {

	using namespace Engine;
	RendererRenderSettings settings;
	from_json({{"stencilReference", -1}, {"stencilReadMask", 256}}, settings);
	if (settings.stencilReference != 0 || settings.stencilReadMask != 255) return false;
	const auto original = settings;
	try {
		from_json({{"depthWrite", false}, {"fillMode", "invalid"}}, settings);
		return false;
	} catch (const std::invalid_argument&) {
		if (settings != original) return false;
	}
	for (uint8_t value = 1; value <= 8; ++value) {
		settings.depthComparison = static_cast<RendererDepthComparison>(value);
		settings.frontFace.fail = static_cast<RendererStencilOperation>(value);
		const nlohmann::json encoded = settings;
		if (encoded.get<RendererRenderSettings>() != settings) return false;
	}
	return CheckComponent<MeshRendererComponent>(14) && CheckComponent<PrimitiveRendererComponent>(20) &&
		CheckComponent<SpriteRendererComponent>(29) && CheckComponent<TextRendererComponent>(30) &&
		CheckComponent<LineRendererComponent>(13) && CheckComponent<ParticleSystemComponent>(8);
}
