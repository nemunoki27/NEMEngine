#include "AnimationClipTool.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/NameComponent.h>
#include <Engine/Core/ECS/Component/Builtin/TransformComponent.h>
#include <Engine/Core/Graphics/Render/RenderPipelineRunner.h>
#include <Engine/Core/Context/EngineContext.h>

// imgui
#include <imgui.h>
// c++
#include <string>

//============================================================================
//	AnimationClipTool classMethods
//============================================================================

void AnimationClipTool::OpenEditorTool() {

	openWindow_ = true;
}

void AnimationClipTool::DrawEditorTool(const EditorToolContext& context) {

	if (!openWindow_) {
		return;
	}
	DrawWindow(context);
}

void AnimationClipTool::DrawWindow(const EditorToolContext& context) {

	if (!ImGui::Begin("Animation Clip", &openWindow_)) {
		ImGui::End();
		return;
	}

	ImGui::TextDisabled("Animation clip editor base");
	ImGui::Separator();

	DrawPreview(context);

	ImGui::End();
}

void AnimationClipTool::DrawPreview(const EditorToolContext& context) {

	// 作成するときは現在のゲームビューと同じサイズで作成を行う
	auto window = EngineContext::GetWindowSetting();
	Vector2I renderTextureSize = window.gameSize;
	if (renderTextureSize.x <= 0 || renderTextureSize.y <= 0) {

		renderTextureSize = kPreviewSize_;
	}
	EditorToolRenderTexture* preview = CreateRenderTexture("AnimationClipPreview",
		renderTextureSize, kPreviewColor_, kPreviewColorTargetCount_);
	if (!preview) {

		ImGui::TextDisabled("Preview RenderTexture is not available.");
		return;
	}

	RenderPreviewEntity(context, *preview);

	ImGui::Text("Preview Entity : %s", GetPreviewEntityName(context, *preview).c_str());

	ImVec2 imageSize(kPreviewSize_.GetFloat().x, kPreviewSize_.GetFloat().y);
	const float availableWidth = ImGui::GetContentRegionAvail().x;
	if (0.0f < availableWidth && availableWidth < imageSize.x) {

		const float scale = availableWidth / imageSize.x;
		imageSize.x *= scale;
		imageSize.y *= scale;
	}

	// ImGui::Imageで表示した直後のアイテムを、HierarchyのEntityドロップ先にする。
	ImGui::Image(preview->GetImTextureID(), imageSize);
	AcceptPreviewEntityDragDrop(context, *preview);

	if (ImGui::IsItemHovered()) {

		ImGui::SetTooltip("Drop Entity from Hierarchy");
	}
}

void AnimationClipTool::RenderPreviewEntity(const EditorToolContext& context, EditorToolRenderTexture& preview) {

	ECSWorld* world = context.GetWorld();
	const Entity previewEntity = GetPreviewEntity(context, preview);
	if (!world || !context.panelContext || !context.panelContext->renderPipeline ||
		!world->IsAlive(previewEntity)) {

		// Entityが未設定の場合も、古い描画結果が残らないように毎フレームクリアする。
		RenderToTexture(preview, [](EditorToolRenderContext&) {
			}, preview.clearColor);
		return;
	}

	RenderToTexture(preview, [&](EditorToolRenderContext& renderContext) {

		Vector3 targetPos = Vector3::AnyInit(0.0f);
		if (world->HasComponent<TransformComponent>(previewEntity)) {

			targetPos = world->GetComponent<TransformComponent>(previewEntity).worldMatrix.GetTranslationValue();
		}

		ManualRenderCameraState camera{};
		camera.enableOrthographic = true;
		camera.orthoNearClip = 0.0f;
		camera.orthoFarClip = 1000.0f;
		camera.orthographicCullingMask = -1;
		camera.transform2D.pos = Vector3(targetPos.x - preview.size.GetFloat().x * 0.5f,
			targetPos.y - preview.size.GetFloat().y * 0.5f, targetPos.z - 10.0f);

		camera.enablePerspective = true;
		camera.perspectiveFovY = 45.0f;
		camera.perspectiveNearClip = 0.01f;
		camera.perspectiveFarClip = 4000.0f;
		camera.perspectiveCullingMask = -1;
		camera.transform3D.pos = targetPos + Vector3(0.0f, 1.5f, -6.0f);
		camera.transform3D.rotation = Vector3(0.0f, 0.0f, 0.0f);

		EntityPreviewRenderRequest request{};
		request.world = world;
		request.systemContext = context.toolContext.systemContext;
		request.assetDatabase = context.toolContext.assetDatabase;
		request.sceneHeader = context.toolContext.activeSceneHeader;
		request.sceneInstanceID = context.toolContext.activeSceneInstanceID;
		request.rootEntity = previewEntity;
		request.surface = preview.GetRenderTarget();
		request.camera = camera;
		request.clearColor = preview.clearColor;

		context.panelContext->renderPipeline->RenderEntityPreview(*renderContext.graphicsCore, request);
		}, preview.clearColor);
}

std::string AnimationClipTool::GetPreviewEntityName(const EditorToolContext& context,
	const EditorToolRenderTexture& preview) const {

	ECSWorld* world = context.GetWorld();
	const Entity entity = GetPreviewEntity(context, preview);
	if (!world || !world->IsAlive(entity)) {
		return "None";
	}

	if (world->HasComponent<NameComponent>(entity)) {
		return world->GetComponent<NameComponent>(entity).name;
	}
	return ToString(world->GetUUID(entity));
}
