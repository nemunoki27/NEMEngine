#include "IInspectorComponentDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Components/RemoveComponentCommand.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

//============================================================================
//	IInspectorComponentDrawer classMethods
//============================================================================
bool Engine::IInspectorComponentDrawer::RemoveComponent(const EditorPanelContext& context,
	ECSWorld& world, const Entity& entity) {

	if (!context.host || !world.IsAlive(entity) || !world.HasComponent(entity, componentTypeName_)) {
		return false;
	}
	CommitPendingEdits(context, world, entity);
	if (!context.host->ExecuteEditorCommand(std::make_unique<RemoveComponentCommand>(entity, componentTypeName_))) {
		return false;
	}
	SyncPreviewOwner(&world, entity);
	EndPreview();
	return true;
}
