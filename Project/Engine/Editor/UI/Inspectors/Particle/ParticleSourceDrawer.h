#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Structures/ParticleSourceSettings.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>

namespace Engine::ParticleSourceDrawer {

	// GPU発生元と表面から引き継ぐ情報を編集する
	bool Draw(const EditorPanelContext& context, ParticleSourceSettings& settings);
}
