#include "VolumeInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>

// c++
#include <algorithm>

//============================================================================
//	VolumeInspectorDrawer classMethods
//============================================================================

void Engine::VolumeInspectorDrawer::DrawFields(const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	VolumeComponent& draft = GetDraft();
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("Global", draft.global);
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::AssetReferenceField("Profile", draft.profile,
			context.editorContext ? context.editorContext->assetDatabase : nullptr,
			{ AssetType::VolumeProfile });
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawLayerMaskField(context, "Volume Layer", draft.layerMask);
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("優先度", draft.priority, { .dragSpeed = 0.1f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("重み", draft.weight,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 1.0f });
		});
	if (!draft.global) {
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragVector3("大きさ", draft.size,
				{ .dragSpeed = 0.01f,.minValue = 0.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("Blend距離", draft.blendDistance,
				{ .dragSpeed = 0.01f,.minValue = 0.0f });
			});
	}
}

void Engine::VolumeInspectorDrawer::OnBeforeCommit(
	[[maybe_unused]] const VolumeComponent& beforeComponent, VolumeComponent& afterComponent) {

	afterComponent.size.x = (std::max)(afterComponent.size.x, 0.0f);
	afterComponent.size.y = (std::max)(afterComponent.size.y, 0.0f);
	afterComponent.size.z = (std::max)(afterComponent.size.z, 0.0f);
	afterComponent.weight = std::clamp(afterComponent.weight, 0.0f, 1.0f);
	afterComponent.blendDistance = (std::max)(afterComponent.blendDistance, 0.0f);
}
