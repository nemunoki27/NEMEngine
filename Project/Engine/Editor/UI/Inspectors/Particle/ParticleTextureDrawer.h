#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleSystemSettings.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>

namespace Engine::ParticleTextureDrawer {

	// PhaseとTrailの共通Texture入力を表示する
	bool Draw(const EditorPanelContext& context, ParticlePhaseMaterialSettings& settings, AssetID materialID);
}
