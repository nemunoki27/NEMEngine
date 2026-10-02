#include "CameraInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>

// c++
#include <algorithm>

namespace {

	void ClampCameraOutput(Engine::CameraCommon& common) {

		common.viewportX = std::clamp(common.viewportX, 0.0f, 0.99f);
		common.viewportY = std::clamp(common.viewportY, 0.0f, 0.99f);
		common.viewportWidth = std::clamp(common.viewportWidth, 0.01f, 1.0f - common.viewportX);
		common.viewportHeight = std::clamp(common.viewportHeight, 0.01f, 1.0f - common.viewportY);
	}
}

//============================================================================
//	CameraInspectorDrawer classMethods
//============================================================================
void Engine::OrthographicCameraInspectorDrawer::DrawFields(const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("有効", draft.common.enabled); });
	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("メインカメラ", draft.common.isMain); });
	DrawField(anyItemActive, [&]() { return MyGUI::DragInt("優先度", draft.common.priority); });
	DrawField(anyItemActive, [&]() { return MyGUI::DragInt("カリングマスク", draft.common.cullingMask); });
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("Viewport X", draft.common.viewportX,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 0.99f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("Viewport Y", draft.common.viewportY,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 0.99f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("Viewport Width", draft.common.viewportWidth,
			{ .dragSpeed = 0.01f,.minValue = 0.01f,.maxValue = 1.0f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("Viewport Height", draft.common.viewportHeight,
			{ .dragSpeed = 0.01f,.minValue = 0.01f,.maxValue = 1.0f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::AssetReferenceField("出力Texture", draft.common.targetTexture,
			context.editorContext ? context.editorContext->assetDatabase : nullptr,
			{ AssetType::RenderTexture });
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("ポストプロセス", draft.common.postProcessEnabled);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawLayerMaskField(context, "Volume Layer", draft.common.volumeLayerMask);
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::AssetReferenceField("Volume Profile", draft.common.volumeProfile,
			context.editorContext ? context.editorContext->assetDatabase : nullptr,
			{ AssetType::VolumeProfile });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::AssetReferenceField("Render Extension", draft.common.renderExtension,
			context.editorContext ? context.editorContext->assetDatabase : nullptr,
			{ AssetType::RenderExtension });
		});

	FloatEditSetting clipSetting{};
	clipSetting.dragSpeed = 0.01f;
	clipSetting.minValue = 0.0f;
	clipSetting.maxValue = 100000.0f;
	DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("近クリップ", draft.nearClip, clipSetting); });
	DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("遠クリップ", draft.farClip, clipSetting); });
	MyGUI::TextMatrix4x4("ビュープロジェクト行列", draft.common.viewProjectionMatrix);
}

void Engine::OrthographicCameraInspectorDrawer::OnBeforeCommit(
	[[maybe_unused]] const OrthographicCameraComponent& beforeComponent,
	OrthographicCameraComponent& afterComponent) {

	afterComponent.nearClip = (std::max)(afterComponent.nearClip, 0.001f);
	afterComponent.farClip = (std::max)(afterComponent.farClip, afterComponent.nearClip + 0.001f);
	ClampCameraOutput(afterComponent.common);
}

void Engine::PerspectiveCameraInspectorDrawer::DrawFields(const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	MyGUI::TextFloat("アスペクト比", draft.common.aspectRatio, 3);
	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("有効", draft.common.enabled); });
	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("メインカメラ", draft.common.isMain); });
	DrawField(anyItemActive, [&]() { return MyGUI::DragInt("優先度", draft.common.priority); });
	DrawField(anyItemActive, [&]() { return MyGUI::DragInt("カリングマスク", draft.common.cullingMask); });
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("Viewport X", draft.common.viewportX,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 0.99f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("Viewport Y", draft.common.viewportY,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 0.99f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("Viewport Width", draft.common.viewportWidth,
			{ .dragSpeed = 0.01f,.minValue = 0.01f,.maxValue = 1.0f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("Viewport Height", draft.common.viewportHeight,
			{ .dragSpeed = 0.01f,.minValue = 0.01f,.maxValue = 1.0f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::AssetReferenceField("出力Texture", draft.common.targetTexture,
			context.editorContext ? context.editorContext->assetDatabase : nullptr,
			{ AssetType::RenderTexture });
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("ポストプロセス", draft.common.postProcessEnabled);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawLayerMaskField(context, "Volume Layer", draft.common.volumeLayerMask);
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::AssetReferenceField("Volume Profile", draft.common.volumeProfile,
			context.editorContext ? context.editorContext->assetDatabase : nullptr,
			{ AssetType::VolumeProfile });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::AssetReferenceField("Render Extension", draft.common.renderExtension,
			context.editorContext ? context.editorContext->assetDatabase : nullptr,
			{ AssetType::RenderExtension });
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawEnumComboField("投影方式", draft.projectionMode);
		});

	FloatEditSetting setting{};
	setting.dragSpeed = 0.01f;
	setting.minValue = 0.001f;
	setting.maxValue = 100000.0f;
	DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("近クリップ", draft.nearClip, setting); });
	DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("遠クリップ", draft.farClip, setting); });
	if (draft.projectionMode == CameraProjectionMode::Perspective) {
		DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("視野角", draft.fovY, setting); });
	} else {
		DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("平行投影サイズ", draft.orthographicSize, setting); });
	}
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("錐台スケール", draft.common.editorFrustumScale,
			{ .dragSpeed = 0.001f,.minValue = 0.0f,.maxValue = 10.0f });
		});
}

void Engine::PerspectiveCameraInspectorDrawer::OnBeforeCommit(
	[[maybe_unused]] const PerspectiveCameraComponent& beforeComponent,
	PerspectiveCameraComponent& afterComponent) {

	afterComponent.nearClip = (std::max)(afterComponent.nearClip, 0.001f);
	afterComponent.farClip = (std::max)(afterComponent.farClip, afterComponent.nearClip + 0.001f);
	afterComponent.orthographicSize = (std::max)(afterComponent.orthographicSize, 0.001f);
	ClampCameraOutput(afterComponent.common);
}
