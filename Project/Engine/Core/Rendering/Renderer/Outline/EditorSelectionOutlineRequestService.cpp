#include "EditorSelectionOutlineRequestService.h"

//============================================================================
//	EditorSelectionOutlineRequestService classMethods
//============================================================================

namespace Engine {

	EditorSelectionOutlineRequestService& EditorSelectionOutlineRequestService::GetInstance() {

		static EditorSelectionOutlineRequestService instance;
		return instance;
	}

	void EditorSelectionOutlineRequestService::Request(ECSWorld* world, const Entity& entity, int32_t subMeshIndex,
		const ScreenSpaceOutlineStyle& style) {

		if (!world) {
			return;
		}
		ScreenSpaceOutlineRequest request{};
		request.world = world;
		request.entity = entity;
		request.subMeshIndex = subMeshIndex;
		request.style = style;
		request.source = ScreenSpaceOutlineSource::EditorSelection;
		requests_.emplace_back(request);
	}
}
