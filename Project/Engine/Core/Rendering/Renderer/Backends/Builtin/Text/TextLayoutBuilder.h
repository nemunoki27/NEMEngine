#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/MSDFFontAsset.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

namespace Engine::TextLayoutBuilder {

	bool NeedsTextLayoutRebuild(const ECSWorld& world, const Entity& entity, const TextRendererComponent& renderer,
		const MSDFFontAsset& font);

	bool RebuildTextLayoutCache(const MSDFFontAsset& font, ECSWorld& world, const Entity& entity,
		TextRendererComponent& renderer);
}
