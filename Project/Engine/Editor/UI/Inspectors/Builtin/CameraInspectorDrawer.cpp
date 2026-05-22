#include "CameraInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>

//============================================================================
//	CameraInspectorDrawer classMethods
//============================================================================

void Engine::OrthographicCameraInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	// ドラフトコンポーネントを参照
	auto& draft = GetDraft();

	//============================================================================
	//	カメラ表示パラメータ
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("有効", draft.common.enabled);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("メインカメラ", draft.common.isMain);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragInt("優先度", draft.common.priority);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragInt("カリングマスク", draft.common.cullingMask);
			});
	}
	{
		FloatEditSetting clipSetting{};
		clipSetting.dragSpeed = 0.01f;
		clipSetting.minValue = 0.001f;
		clipSetting.maxValue = 100000.0f;
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("近クリップ", draft.nearClip, clipSetting);
			});

		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("遠クリップ", draft.farClip, clipSetting);
			});
	}
}

void Engine::OrthographicCameraInspectorDrawer::OnBeforeCommit([[maybe_unused]] const OrthographicCameraComponent& beforeComponent,
	OrthographicCameraComponent& afterComponent) {

	// 値のクランプ
	afterComponent.nearClip = (std::max)(afterComponent.nearClip, 0.001f);
	afterComponent.farClip = (std::max)(afterComponent.farClip, afterComponent.nearClip + 0.001f);
}

void Engine::PerspectiveCameraInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	// ドラフトコンポーネントを参照
	auto& draft = GetDraft();

	//============================================================================
	//	カメラ表示パラメータ
	//============================================================================
	{
		MyGUI::TextFloat("アスペクト比", draft.common.aspectRatio, 3);
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("有効", draft.common.enabled);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("メインカメラ", draft.common.isMain);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragInt("優先度", draft.common.priority);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragInt("カリングマスク", draft.common.cullingMask);
			});
	}
	{
		FloatEditSetting setting{};
		setting.dragSpeed = 0.01f;
		setting.minValue = 0.001f;
		setting.maxValue = 100000.0f;
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("近クリップ", draft.nearClip, setting);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("遠クリップ", draft.farClip, setting);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("視野角", draft.fovY, setting);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("錐台スケール", draft.common.editorFrustumScale,
				{ .dragSpeed = 0.001f,.minValue = 0.0f,.maxValue = 10.0f });
			});
	}
}

void Engine::PerspectiveCameraInspectorDrawer::OnBeforeCommit([[maybe_unused]] const PerspectiveCameraComponent& beforeComponent,
	PerspectiveCameraComponent& afterComponent) {

	// 値のクランプ
	afterComponent.nearClip = (std::max)(afterComponent.nearClip, 0.001f);
	afterComponent.farClip = (std::max)(afterComponent.farClip, afterComponent.nearClip + 0.001f);
}
