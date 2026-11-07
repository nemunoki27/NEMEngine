#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/RendererRenderSettings.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>

namespace Engine {

	class ECSWorld;
	// 描画コンポーネントの設定を参照する
	RendererRenderSettings* ResolveRendererSettings(ECSWorld& world, Entity entity, int32_t type);
}
