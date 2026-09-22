#pragma once

//============================================================================
//	include
//============================================================================
#include "UIInputTypes.h"

namespace Engine::UIElementSelection {

	UUID GetLocalFileID(ECSWorld& world, Entity entity);

	Vector2 ResolveElementCenter(ECSWorld& world, Entity entity, const UIElementRuntime& runtime);

	UISelectableEntry* FindEntryByLocalFileID(ECSWorld& world,
		std::vector<UISelectableEntry>& entries, Entity canvas, UUID localFileID);

	UISelectableEntry* FindFirstInteractable(std::vector<UISelectableEntry>& entries, Entity canvas);

	UISelectableEntry* FindAutomaticNavigation(std::vector<UISelectableEntry>& entries,
		const UISelectableEntry& current, const Vector2& direction, bool wrap);

	bool IsTransitionTableEntry(ECSWorld& world, std::span<const CanvasNavigationCell> cells,
		const UISelectableEntry& entry);

	UISelectableEntry* FindFirstTransitionTableEntry(ECSWorld& world,
		std::vector<UISelectableEntry>& entries, Entity canvasEntity, std::span<const CanvasNavigationCell> cells);

	UISelectableEntry* FindTransitionTableNavigation(ECSWorld& world,
		std::vector<UISelectableEntry>& entries, Entity canvasEntity,
		const CanvasComponent& canvas, const UISelectableEntry& current, const Vector2& direction,
		std::span<const CanvasNavigationCell> cells);
}
