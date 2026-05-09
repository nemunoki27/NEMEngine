#include "AnimationClipTool.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/TransformComponent.h>
#include <Engine/Core/ECS/Component/Builtin/CameraComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Render/SpriteRendererComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Render/TextRendererComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Render/MeshRendererComponent.h>
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

	// ウィンドウ起動
	openWindow_ = true;
}

void AnimationClipTool::DrawEditorTool(const EditorToolContext& context) {

	if (!openWindow_) {
		return;
	}

	if (!ImGui::Begin("Animation Clip", &openWindow_)) {
		ImGui::End();
		return;
	}

	//============================================================================
	//	レンダーターゲットの作成、設定
	//============================================================================

	// 作成するときは現在のゲームビューと同じサイズで作成を行う
	Vector2I renderTextureSize = EngineContext::GetWindowSetting().gameSize;
	if (renderTextureSize.x <= 0 || renderTextureSize.y <= 0) {

		renderTextureSize = kPreviewSize_;
	}
	EditorToolRenderTexture* preview = CreateRenderTexture("AnimationClipPreview",
		renderTextureSize, kPreviewColor_, kPreviewColorTargetCount_);
	if (!preview) {

		ImGui::TextDisabled("Preview RenderTexture is not available.");
		ImGui::End();
		return;
	}

	//============================================================================
	//	プレビューをレンダーターゲットに対して描画
	//============================================================================

	RenderPreviewEntity(context, *preview);

	//============================================================================
	//	描画結果を表示
	//============================================================================

	ImGui::Image(preview->GetImTextureID(), ImVec2(kPreviewSize_.GetFloat().x, kPreviewSize_.GetFloat().y));
	// Imageをエンティティドロップのターゲットにする
	AcceptPreviewEntityDragDrop(context, *preview);

	ImGui::End();
}

void AnimationClipTool::RenderPreviewEntity(const EditorToolContext& context, EditorToolRenderTexture& preview) {

	ECSWorld* world = context.GetWorld();
	// プレビュー用に設定しているエンティティを取得
	const Entity previewEntity = GetPreviewEntity(context, preview);
	// エンティティがワールド内で存在しない場合
	if (!world || !world->IsAlive(previewEntity)) {
		// エンティティが未設定の場合も、古い描画結果が残らないように毎フレームクリアする。
		RenderToTexture(preview, [](EditorToolRenderContext&) {}, preview.clearColor);
		return;
	}

	// レンダーターゲットへ描画する中身
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

		// プレビュー描画のリクエストを構築
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
		// グリッド描画
		DrawGrid(*world, previewEntity, request);
		// プレビューエンティティを描画する
		context.panelContext->renderPipeline->RenderEntityPreview(*renderContext.graphicsCore, request);
		}, preview.clearColor);
}

void Engine::AnimationClipTool::DrawGrid(const ECSWorld& world,
	const Entity& previewEntity, EntityPreviewRenderRequest& request) {

	// グリッドを描画
	// 3D
	if (world.HasComponent<MeshRendererComponent>(previewEntity) ||
		world.HasComponent<PerspectiveCameraComponent>(previewEntity)) {

		// プレビュー描画側で、AnimationClipTool用カメラとRenderTextureに対して描画する。
		request.drawGrid3D = true;
	}
	// 2D
	else if (world.HasComponent<SpriteRendererComponent>(previewEntity) ||
		world.HasComponent<TextRendererComponent>(previewEntity) ||
		world.HasComponent<OrthographicCameraComponent>(previewEntity)) {

		// プレビュー描画側で、AnimationClipTool用カメラとRenderTextureに対して描画する。
		request.drawGrid2D = true;
	}
}