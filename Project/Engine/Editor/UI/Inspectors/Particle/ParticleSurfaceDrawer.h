#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Structures/ParticleSurfaceSettings.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>

namespace Engine::ParticleSurfaceDrawer {

	// 深度フェードと表面の受光を編集する
	bool Draw(const EditorPanelContext& context, ParticleSurfaceSettings& settings);
}
