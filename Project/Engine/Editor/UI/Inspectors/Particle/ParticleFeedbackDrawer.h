#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Structures/ParticleFeedbackSettings.h>

namespace Engine {

	struct EditorPanelContext;
	class ECSWorld;

	namespace ParticleFeedbackDrawer {

		// 衝突の出力先と次回更新の発生経路を編集する
		bool Draw(const EditorPanelContext& context, ECSWorld& world, Entity owner, ParticleFeedbackSettings& settings);
	}
}
