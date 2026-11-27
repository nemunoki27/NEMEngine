#pragma once

#include <Engine/Core/World/Components/Rendering/MaterialPaintComponent.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>

namespace Engine {

	class ECSWorld;

	// 編集中のストロークだけを描画に反映する
	void SetMaterialPaintPreview(const ECSWorld& world, Entity entity, const MaterialPaintComponent& component);
	void ClearMaterialPaintPreviews(const ECSWorld& world);
}
