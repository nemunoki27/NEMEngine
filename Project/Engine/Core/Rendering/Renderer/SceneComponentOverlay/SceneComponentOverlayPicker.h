#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>

namespace Engine {

	class ECSWorld;

	//============================================================================
	//	SceneComponentOverlayPicker class
	//	SceneView専用OverlayのCPUピックを担当する
	//============================================================================
	class SceneComponentOverlayPicker {
	public:
		// SceneView座標のマウス位置からOverlayをEntity単位で選択する
		bool Pick(ECSWorld* world, const ResolvedRenderView& view,
			const Vector2& inputPixel, Entity& outEntity) const;
	};
}
