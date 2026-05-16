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
#include <Engine/Core/Runtime/RuntimePaths.h>
#include <Engine/Core/Input/Input.h>

// imgui
#include <imgui.h>
// c++
#include <string>

//============================================================================
//	AnimationClipTool classMethods
//============================================================================

namespace {

	// エンティティが持っているコンポーネントで次元を取得
	Dimension GetDimension(const ECSWorld& world, const Entity& previewEntity) {

		Dimension result{};

		// 3D
		if (world.HasComponent<MeshRendererComponent>(previewEntity) ||
			world.HasComponent<PerspectiveCameraComponent>(previewEntity)) {

			result = Dimension::Type3D;
		}
		// 2D
		else if (world.HasComponent<SpriteRendererComponent>(previewEntity) ||
			world.HasComponent<TextRendererComponent>(previewEntity) ||
			world.HasComponent<OrthographicCameraComponent>(previewEntity)) {

			result = Dimension::Type2D;
		}
		return result;
	}

	// カメラ保存パス
	const std::string kCameraJsonPath = "Tools/AnimationClip/cameraController.exeConfig.json";
}

Engine::AnimationClipTool::AnimationClipTool() {

	// カメラ操作初期化
	cameraController_ = std::make_unique<SceneViewCameraController>();
	cameraController_->MakeDefaultState();
	cameraController_->SetSavePath(RuntimePaths::GetEngineAssetPath(kCameraJsonPath).string());
}

void AnimationClipTool::OpenEditorTool() {

	// カメラ操作初期化
	cameraController_->MakeFromJson(RuntimePaths::GetEngineAssetPath(kCameraJsonPath).string());

	// ウィンドウ起動
	openWindow_ = true;
}

void AnimationClipTool::DrawEditorTool(const EditorToolContext& context) {

	if (!openWindow_) {
		return;
	}

	if (!ImGui::Begin("AnimationClip Clip", &openWindow_)) {
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
	if (!world->IsAlive(previewEntity)) {
		// エンティティが未設定の場合も、古い描画結果が残らないように毎フレームクリアする。
		RenderToTexture(preview, [](EditorToolRenderContext&) {}, preview.clearColor);
		return;
	}

	// エンティティの次元を取得
	previewDimension_ = GetDimension(*world, previewEntity);
	// ビューポートの描画領域を入力システムに同期
	imagePos_ = ImGui::GetCursorScreenPos();
	Input::GetInstance()->SetViewRect(InputViewArea::AnimationClip, Vector2(imagePos_.x, imagePos_.y),
		kPreviewSize_.GetFloat(), EngineContext::GetWindowSetting().gameSize.GetFloat());

	// レンダーターゲットへ描画する中身
	RenderToTexture(preview, [&](EditorToolRenderContext& renderContext) {

		// カメラ操作更新
		cameraController_->Update(previewDimension_, InputViewArea::AnimationClip);

		// プレビュー描画のリクエストを構築
		EntityPreviewRenderRequest request{};
		request.world = world;
		request.systemContext = context.toolContext.systemContext;
		request.assetDatabase = context.toolContext.assetDatabase;
		request.sceneHeader = context.toolContext.activeSceneHeader;
		request.sceneInstanceID = context.toolContext.activeSceneInstanceID;
		request.rootEntity = previewEntity;
		request.surface = preview.GetRenderTarget();
		request.camera = cameraController_->GetCameraState();
		request.clearColor = preview.clearColor;
		// グリッド描画
		DrawGrid(request);
		// プレビューエンティティを描画する
		context.panelContext->renderPipeline->RenderEntityPreview(*renderContext.graphicsCore, request);
		}, preview.clearColor);
}

void Engine::AnimationClipTool::DrawGrid(EntityPreviewRenderRequest& request) {

	// グリッドを描画
	// 3D
	if (previewDimension_ == Dimension::Type3D) {

		request.drawGrid3D = true;
	}
	// 2D
	else if (previewDimension_ == Dimension::Type2D) {

		request.drawGrid2D = true;
	}
}