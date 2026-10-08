#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>
#include <Engine/Editor/Tools/Core/EditorToolContext.h>

namespace Engine::ParticleEffectTextureDrawer {

	// PhaseとTrailの共通Texture入力を表示する
	bool Draw(const EditorToolContext& context, ParticlePhaseMaterialSettings& settings, AssetID materialID);
}
