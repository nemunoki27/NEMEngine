#include "ViewportPanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/View/ViewportRenderService.h>
#include <Engine/Core/Graphics/Render/Texture/RenderTexture2D.h>
#include <Engine/Core/ECS/Component/Builtin/HierarchyComponent.h>
#include <Engine/Core/ECS/Component/Builtin/CameraComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Light/DirectionalLightComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Light/PointLightComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Light/SpotLightComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Render/MeshRendererComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Render/SpriteRendererComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Render/TextRendererComponent.h>
#include <Engine/Editor/Command/Methods/SetSerializedComponentCommand.h>
#include <Engine/Editor/Command/Methods/SetTransformCommand.h>
#include <Engine/Editor/Command/TransformEditUtility.h>
#include <Engine/Editor/Panel/Interface/IEditorPanelHost.h>
#include <Engine/Utility/ImGui/MyGUI.h>
#include <Engine/Input/Input.h>

//============================================================================
//	ViewportPanel classMethods
//============================================================================

namespace {

	bool Prefers2DGizmo(const Engine::EditorPanelContext& context,
		Engine::ECSWorld& world, const Engine::Entity& entity) {

		// 2D描画に関係するコンポーネントがある場合は2Dギズモを優先
		if (world.HasComponent<Engine::SpriteRendererComponent>(entity) ||
			world.HasComponent<Engine::TextRendererComponent>(entity) ||
			world.HasComponent<Engine::OrthographicCameraComponent>(entity)) {
			return true;
		}
		if (world.HasComponent<Engine::MeshRendererComponent>(entity) ||
			world.HasComponent<Engine::PerspectiveCameraComponent>(entity) ||
			world.HasComponent<Engine::DirectionalLightComponent>(entity) ||
			world.HasComponent<Engine::PointLightComponent>(entity) ||
			world.HasComponent<Engine::SpotLightComponent>(entity)) {
			return false;
		}
		return context.editorState && context.editorState->manualCameraDimension == Engine::Dimension::Type2D;
	}
	// シーンギズモの描画に使用するカメラビューを選択する
	const Engine::ResolvedCameraView* SelectSceneGizmoCamera(const Engine::ResolvedRenderView& view, bool prefer2DTarget) {

		if (prefer2DTarget) {
			if (view.orthographic.valid) {
				return &view.orthographic;
			}
			if (view.perspective.valid) {
				return &view.perspective;
			}
		} else {
			if (view.perspective.valid) {
				return &view.perspective;
			}
			if (view.orthographic.valid) {
				return &view.orthographic;
			}
		}
		return nullptr;
	}
	// エンティティの親のワールド行列を取得する
	Engine::Matrix4x4 GetEntityParentWorldMatrix(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (!world.IsAlive(entity) || !world.HasComponent<Engine::HierarchyComponent>(entity)) {
			return Engine::Matrix4x4::Identity();
		}
		const auto& hierarchy = world.GetComponent<Engine::HierarchyComponent>(entity);
		if (!world.IsAlive(hierarchy.parent) || !world.HasComponent<Engine::TransformComponent>(hierarchy.parent)) {

			return Engine::Matrix4x4::Identity();
		}
		return world.GetComponent<Engine::TransformComponent>(hierarchy.parent).worldMatrix;
	}
	// メッシュレンダラーのプレビュー設定をエンティティに適用する
	void ApplyMeshRendererPreview(Engine::ECSWorld& world, const Engine::Entity& entity,
		const Engine::MeshRendererComponent& previewRenderer) {

		if (!world.IsAlive(entity) || !world.HasComponent<Engine::MeshRendererComponent>(entity)) {
			return;
		}

		Engine::MeshRendererComponent updated = previewRenderer;

		Engine::Matrix4x4 parentWorld = Engine::Matrix4x4::Identity();
		if (world.HasComponent<Engine::TransformComponent>(entity)) {

			parentWorld = world.GetComponent<Engine::TransformComponent>(entity).worldMatrix;
		}
		Engine::MeshSubMeshRuntime::UpdateRendererRuntime(updated, parentWorld);
		world.GetComponent<Engine::MeshRendererComponent>(entity) = std::move(updated);
	}
	// 現在の選択からサブメッシュ選択を解決する
	bool TryResolveSelectedSubMesh(const Engine::EditorPanelContext& context, Engine::ECSWorld& world,
		Engine::Entity& outEntity, uint32_t& outSubMeshIndex, Engine::UUID& outStableID) {

		if (!context.editorState || !context.editorState->HasValidSubMeshSelection(&world)) {
			return false;
		}

		outEntity = context.editorState->selectedEntity;
		if (!world.IsAlive(outEntity) || !world.HasComponent<Engine::MeshRendererComponent>(outEntity)) {
			return false;
		}

		if (!context.editorState->TryResolveSelectedSubMeshIndex(&world, outSubMeshIndex)) {
			return false;
		}

		const auto& renderer = world.GetComponent<Engine::MeshRendererComponent>(outEntity);
		if (renderer.subMeshes.size() <= outSubMeshIndex) {
			return false;
		}
		outStableID = renderer.subMeshes[outSubMeshIndex].stableID;
		return true;
	}
}

Engine::ViewportPanel::ViewportPanel(const char* windowName, const char* label, ViewportPanelKind kind) :
	windowName_(windowName), label_(label), kind_(kind) {}

void Engine::ViewportPanel::Draw(const EditorPanelContext& context) {

	bool* visible = nullptr;
	ImVec2* lastSize = nullptr;
	switch (kind_) {
	case ViewportPanelKind::Scene:

		visible = &context.layoutState->showSceneView;
		lastSize = &context.layoutState->lastSceneViewSize;
		break;
	case ViewportPanelKind::Game:

		visible = &context.layoutState->showGameView;
		lastSize = &context.layoutState->lastGameViewSize;
		break;
	}
	// 無効な種類の場合は何もしない
	if (!visible) {
		return;
	}

	if (!ImGui::Begin(windowName_.c_str(), visible,
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove)) {
		ImGui::End();
		return;
	}

	// ビューポートの内容を描画する
	*lastSize = ImGui::GetContentRegionAvail();
	DrawViewportContent(context, label_.c_str(), *lastSize);

	ImGui::End();
}

void Engine::ViewportPanel::DrawViewportContent(const EditorPanelContext& context, const char* id, const ImVec2& size) {

	// 描画領域のサイズが小さすぎる場合は、最小サイズを1x1にする
	ImVec2 region = size;

	ImGui::BeginChild(id, region, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	// ビューポートの種類に応じた描画ビューのサーフェスを取得
	RenderViewKind viewKind = (kind_ == ViewportPanelKind::Game) ? RenderViewKind::Game : RenderViewKind::Scene;
	InputViewArea inputArea = (kind_ == ViewportPanelKind::Game) ? InputViewArea::Game : InputViewArea::Scene;

	// 描画領域の位置とサイズを取得
	ImVec2 imagePos = ImGui::GetCursorScreenPos();
	if (const RenderTexture2D* display = context.viewportRenderService->GetDisplayTexture(viewKind)) {

		// 表示サイズ
		Vector2 srcSize(static_cast<float>(display->GetRenderTarget().width), static_cast<float>(display->GetRenderTarget().height));
		// ビューポートの描画領域を入力システムに同期
		SyncInputViewRect(inputArea, imagePos, viewSize_, srcSize);

		// 描画ビューのサーフェスをImGuiに描画
		ImGui::Image(static_cast<ImTextureID>(display->GetSRVGPUHandle().ptr), viewSize_);

		// シーンビューの場合はシーンギズモも描画
		if (kind_ == ViewportPanelKind::Scene) {

			DrawSceneGizmo(context);
		}
	}
	ImGui::EndChild();
}

void Engine::ViewportPanel::SyncInputViewRect(InputViewArea area, const ImVec2& screenPos,
	const ImVec2& drawSize, const Vector2& srcSize) const {

	// ビューポートの描画領域を入力システムに同期
	Input::GetInstance()->SetViewRect(area, Vector2(screenPos.x, screenPos.y),
		Vector2(drawSize.x, drawSize.y), srcSize);
}

void Engine::ViewportPanel::DrawSceneGizmo(const EditorPanelContext& context) {

	// フラグリセット
	context.editorState->useSceneGizmo = false;

	ECSWorld* worldPtr = context.GetWorld();
	if (!worldPtr) {
		return;
	}
	ECSWorld& world = *worldPtr;

	// 編集不可の場合はギズモセッションを終了して何もしない
	if (!context.CanEditScene() || !context.sceneRenderView || !context.sceneRenderView->valid ||
		context.editorState->sceneViewManipulatorMode == SceneViewManipulatorMode::None) {
		FinalizeSubMeshGizmoSession(context, world);
		FinalizeEntityGizmoSession(context, world);
		return;
	}
	// シーンビューのマニピュレーター選択が「なし」の場合はギズモセッションを終了して何もしない
	if (context.editorState->sceneViewManipulatorMode == SceneViewManipulatorMode::None) {
		FinalizeSubMeshGizmoSession(context, world);
		FinalizeEntityGizmoSession(context, world);
		return;
	}

	// 現在のマニピュレーター操作を取得
	const ImVec2 rectMin = ImGui::GetItemRectMin();
	const ImVec2 rectMax = ImGui::GetItemRectMax();

	// ギズモの描画に必要な情報をまとめた構造体を作成
	GizmoViewportRect rect{};
	rect.x = rectMin.x;
	rect.y = rectMin.y;
	rect.width = rectMax.x - rectMin.x;
	rect.height = rectMax.y - rectMin.y;
	// 描画領域が有効でない場合はギズモセッションを終了して何もしない
	if (!rect.IsValid()) {
		FinalizeSubMeshGizmoSession(context, world);
		FinalizeEntityGizmoSession(context, world);
		return;
	}

	//========================================================================
	//	サブメッシュ選択中
	//========================================================================
	{
		Entity entity = Entity::Null();
		uint32_t subMeshIndex = 0;
		UUID subMeshStableID{};
		// 現在の選択からサブメッシュ選択を解決する
		if (TryResolveSelectedSubMesh(context, world, entity, subMeshIndex, subMeshStableID)) {

			// 別セッションなら先に閉じる
			if (entityGizmoSession_.active) {
				FinalizeEntityGizmoSession(context, world);
			}
			if (subMeshGizmoSession_.active && (subMeshGizmoSession_.entityUUID != world.GetUUID(entity) ||
				subMeshGizmoSession_.subMeshStableID != subMeshStableID)) {
				FinalizeSubMeshGizmoSession(context, world);
			}

			// ギズモの描画に使用するカメラビューを選択する
			const ResolvedCameraView* camera = SelectSceneGizmoCamera(*context.sceneRenderView, false);
			if (!camera) {
				FinalizeSubMeshGizmoSession(context, world);
				return;
			}

			// サブメッシュのプレビュー設定を取得する
			MeshRendererComponent previewRenderer = world.GetComponent<MeshRendererComponent>(entity);
			// サブメッシュのランタイム情報を更新する
			Matrix4x4 parentWorld = world.HasComponent<TransformComponent>(entity) ?
				world.GetComponent<TransformComponent>(entity).worldMatrix : Matrix4x4::Identity();
			MeshSubMeshRuntime::UpdateRendererRuntime(previewRenderer, parentWorld);
			if (previewRenderer.subMeshes.size() <= subMeshIndex) {
				FinalizeSubMeshGizmoSession(context, world);
				return;
			}

			// ギズモの描画に必要な情報をまとめた構造体を作成
			GizmoViewContext gizmoContext{};
			gizmoContext.rect = rect;
			gizmoContext.viewMatrix = camera->matrices.viewMatrix;
			gizmoContext.projectionMatrix = camera->matrices.projectionMatrix;
			gizmoContext.parentWorldMatrix = world.HasComponent<TransformComponent>(entity) ?
				world.GetComponent<TransformComponent>(entity).worldMatrix : Matrix4x4::Identity();
			gizmoContext.mode = context.editorState->sceneViewManipulatorMode;
			gizmoContext.orthographic = camera == &context.sceneRenderView->orthographic;
			gizmoContext.allowAxisFlip = true;

			auto& subMesh = previewRenderer.subMeshes[subMeshIndex];

			// ギズモを描画し、操作結果を取得する
			const GizmoEditResult result = MyGUI::Manipulate3D("##SceneSubMeshGizmo", gizmoContext, subMesh);

			// 使用しているか
			context.editorState->useSceneGizmo = result.IsUse();

			if (result.isUsing && !subMeshGizmoSession_.active) {

				subMeshGizmoSession_.entityUUID = world.GetUUID(entity);
				subMeshGizmoSession_.subMeshStableID = subMeshStableID;
				subMeshGizmoSession_.active = world.SerializeComponentToJson(entity, "MeshRenderer", subMeshGizmoSession_.beforeMeshRenderer);
			}
			// 値が変更された場合はプレビュー設定をエンティティに適用する
			if (result.valueChanged) {

				MeshSubMeshRuntime::UpdateSubMeshRuntime(subMesh, parentWorld);
				world.GetComponent<MeshRendererComponent>(entity) = std::move(previewRenderer);
			}
			// 使用を終了した場合はセッションを終了する
			if (subMeshGizmoSession_.active && !result.isUsing) {

				FinalizeSubMeshGizmoSession(context, world);
			}
			return;
		}
	}

	//========================================================================
	//	エンティティ選択中
	//========================================================================
	{
		const Entity entity = context.editorState->selectedEntity;
		// 編集不可なエンティティの場合はギズモセッションを終了して何もしない
		if (!world.IsAlive(entity) || !world.HasComponent<TransformComponent>(entity)) {
			FinalizeSubMeshGizmoSession(context, world);
			FinalizeEntityGizmoSession(context, world);
			return;
		}
		// 別セッションなら先に閉じる
		if (subMeshGizmoSession_.active) {
			FinalizeSubMeshGizmoSession(context, world);
		}
		if (entityGizmoSession_.active && entityGizmoSession_.entityUUID != world.GetUUID(entity)) {
			FinalizeEntityGizmoSession(context, world);
		}

		// 現在のマニピュレーター操作を取得
		bool use2DTarget = Prefers2DGizmo(context, world, entity);
		const ResolvedCameraView* camera = SelectSceneGizmoCamera(*context.sceneRenderView, use2DTarget);
		if (!camera) {
			FinalizeEntityGizmoSession(context, world);
			return;
		}

		// ギズモの描画に必要な情報をまとめた構造体を作成
		GizmoViewContext gizmoContext{};
		gizmoContext.rect = rect;
		gizmoContext.viewMatrix = camera->matrices.viewMatrix;
		gizmoContext.projectionMatrix = camera->matrices.projectionMatrix;
		gizmoContext.parentWorldMatrix = GetEntityParentWorldMatrix(world, entity);
		gizmoContext.mode = context.editorState->sceneViewManipulatorMode;
		gizmoContext.orthographic = camera == &context.sceneRenderView->orthographic;
		gizmoContext.allowAxisFlip = !use2DTarget;

		TransformComponent previewTransform = world.GetComponent<TransformComponent>(entity);

		// ギズモを描画し、操作結果を取得する
		const GizmoEditResult result = use2DTarget ? MyGUI::Manipulate2D("##SceneEntityGizmo2D", gizmoContext, previewTransform) :
			MyGUI::Manipulate3D("##SceneEntityGizmo3D", gizmoContext, previewTransform);

		// 使用しているか
		context.editorState->useSceneGizmo = result.IsUse();

		if (result.isUsing && !entityGizmoSession_.active) {

			entityGizmoSession_.active = true;
			entityGizmoSession_.entityUUID = world.GetUUID(entity);
			entityGizmoSession_.beforeTransform = world.GetComponent<TransformComponent>(entity);
		}

		// 値が変更された場合はプレビュー設定をエンティティに適用する
		if (result.valueChanged) {

			TransformEditUtility::ApplyImmediate(world, entity, previewTransform);
		}
		// 使用を終了した場合はセッションを終了する
		if (entityGizmoSession_.active && !result.isUsing) {

			FinalizeEntityGizmoSession(context, world);
		}
	}
}

void Engine::ViewportPanel::FinalizeEntityGizmoSession(const EditorPanelContext& context, ECSWorld& world) {

	if (!entityGizmoSession_.active) {
		return;
	}

	if (context.host) {
		const Entity entity = world.FindByUUID(entityGizmoSession_.entityUUID);
		if (world.IsAlive(entity) && world.HasComponent<TransformComponent>(entity)) {

			const TransformComponent afterTransform = world.GetComponent<TransformComponent>(entity);
			// トランスフォームの値が変更されている場合はコマンドを実行して変更を記録する
			if (!SetTransformCommand::NearlyEqualTransform(entityGizmoSession_.beforeTransform, afterTransform)) {

				context.host->ExecuteEditorCommand(std::make_unique<SetTransformCommand>(entity,
					entityGizmoSession_.beforeTransform, afterTransform));
			}
		}
	}
	entityGizmoSession_ = {};
}

void Engine::ViewportPanel::FinalizeSubMeshGizmoSession(const EditorPanelContext& context, ECSWorld& world) {

	if (!subMeshGizmoSession_.active) {
		return;
	}

	if (context.host) {
		const Entity entity = world.FindByUUID(subMeshGizmoSession_.entityUUID);
		if (world.IsAlive(entity) && world.HasComponent<MeshRendererComponent>(entity)) {

			nlohmann::json afterData{};
			// メッシュレンダラーの値が変更されている場合はコマンドを実行して変更を記録する
			if (world.SerializeComponentToJson(entity, "MeshRenderer", afterData) &&
				subMeshGizmoSession_.beforeMeshRenderer != afterData) {

				context.host->ExecuteEditorCommand(std::make_unique<SetSerializedComponentCommand>(entity,
					"MeshRenderer", subMeshGizmoSession_.beforeMeshRenderer, afterData));
			}
		}
	}
	subMeshGizmoSession_ = {};
}