#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/EditorToolContext.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>

namespace Engine::ParticleEffectPreviewOperations {

	bool UsesEffect(const Engine::ParticleSystemComponent& component, Engine::AssetID effectID);
	const Engine::ParticleEffectInstanceRuntime* ResolveEffectInstance(
		const Engine::ECSWorld& world, const Engine::Entity& entity,
		const Engine::ParticleSystemComponent& component,
		Engine::AssetID effectID);
	// 対象Effectを使用するSystemへ再生を要求する
	void RestartParticleSystems(const EditorToolContext& context, AssetID effectID, std::string& statusMessage, bool oneShot);
	// 対象Effectを使用するSystemへ停止を要求する
	void StopParticleSystems(const EditorToolContext& context, AssetID effectID, std::string& statusMessage);
}
