#include "ViewportPlacementSession.h"
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Scene/Serialization/SceneCreationScope.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Editor/Utility/AssetEntityFactory.h>
#include <Engine/Editor/Commands/Entity/EditorEntitySnapshot.h>
#include <Engine/Editor/Commands/Entity/CreateDroppedEntityCommand.h>
#include <Engine/Editor/Commands/Transform/TransformEditUtility.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Core/Platform/Input/InputSystem.h>
#include "ViewportTransformUtility.h"

// c++
#include <cmath>
#include <algorithm>
#include <optional>

using namespace Engine::ViewportTransformUtility;

void Engine::ViewportPlacementSession::HandleAssetDropPlacement(const EditorPanelContext& context, RenderViewKind viewKind,
	const ImVec2& imagePos, uint32_t renderWidth, uint32_t renderHeight, [[maybe_unused]] bool imageHovered,
	const ImVec2& imageSize, bool scenePanel) {

	viewSize_ = imageSize;

	ECSWorld* world = context.GetWorld();
	AssetDatabase* database = context.editorContext ? context.editorContext->assetDatabase : nullptr;

	// ドラッグ中の画像との重なりを矩形で判定
	const ImVec2 mousePos = ImGui::GetMousePos();
	const bool overImage = mousePos.x >= imagePos.x && mousePos.x <= imagePos.x + viewSize_.x && mousePos.y >= imagePos.y &&
						   mousePos.y <= imagePos.y + viewSize_.y;

	// ドラッグ中のAssetを取得
	const ImGuiPayload* dragging = ImGui::GetDragDropPayload();
	const bool draggingAsset = dragging && dragging->IsDataType(IEditorPanel::kProjectAssetDragDropPayloadType) &&
							   dragging->Data && dragging->DataSize == static_cast<int>(sizeof(EditorAssetDragDropPayload));
	const EditorAssetDragDropPayload* assetPayload =
		draggingAsset ? static_cast<const EditorAssetDragDropPayload*>(dragging->Data) : nullptr;

	// ドラッグが終わったらキャンセル状態を解除する
	if (!draggingAsset) {
		dropPreviewCanceled_ = false;
	}
	// 右クリックでこのドラッグのプレビューをキャンセルする
	if (draggingAsset && overImage && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
		dropPreviewCanceled_ = true;
	}

	// 編集可能な画像上で配置プレビューを生成
	const bool canPreview = draggingAsset && overImage && !dropPreviewCanceled_ && assetPayload && world && database &&
							context.graphicsCore && context.CanEditScene() && AssetEntityFactory::CanSpawn(*assetPayload);

	if (canPreview) {

		// アセットが変わった、または別ワールドのときは作り直す
		if (!preview_.IsActive() || dropPreviewAsset_ != assetPayload->assetID || !preview_.BelongsTo(*world)) {

			DestroyDropPreview();
			SceneCreationScope creation(*world);
			HierarchySystem hierarchySystem{};
			const AssetSpawnResult spawn = AssetEntityFactory::Spawn(*world, *database, *context.graphicsCore, hierarchySystem,
				*assetPayload, context.editorContext->activeSceneInstanceID);
			if (spawn.valid && preview_.Begin(*world, spawn.root)) {

				dropPreviewIsThreeD_ = spawn.isThreeD;
				dropPreviewAsset_ = assetPayload->assetID;
				creation.Commit();
			}
		}
		// プレビューの配置座標を更新
		if (preview_.IsActive() && world->IsAlive(preview_.GetEntity()) &&
			world->HasComponent<TransformComponent>(preview_.GetEntity())) {

			Vector3 position =
				ComputeDropPosition(context, viewKind, dropPreviewIsThreeD_, imagePos, renderWidth, renderHeight);
			ApplyDropSnap(context, position, dropPreviewIsThreeD_);
			auto& transform = world->GetComponent<TransformComponent>(preview_.GetEntity());
			transform.localPos = position;
			MarkTransformSubtreeDirty(*world, preview_.GetEntity());
		}
	} else if (preview_.IsActive()) {

		// ビュー外/キャンセル/ドラッグ終了でプレビューを片付ける
		DestroyDropPreview();
	}

	// ドロップ確定、Imageの上で離されたときだけ受理する
	if (ImGui::BeginDragDropTarget()) {

		if (const ImGuiPayload* accepted = ImGui::AcceptDragDropPayload(IEditorPanel::kProjectAssetDragDropPayloadType)) {

			if (!dropPreviewCanceled_ && preview_.IsActive() && world && world->IsAlive(preview_.GetEntity())) {

				// 履歴への登録成功後に仮Entityの所有を渡す
				if (accepted->IsDelivery() && context.CanEditScene() && context.host && preview_.BelongsTo(*world) &&
					context.host->ExecuteEditorCommand(std::make_unique<CreateDroppedEntityCommand>(preview_.GetEntity()))) {

					const Entity entity = preview_.Release();
					if (context.editorState) {
						context.editorState->SelectEntity(entity);
					}
					dropPreviewAsset_ = AssetID{};
				}
			}
		}
		ImGui::EndDragDropTarget();
	}

	// 配置中のSceneViewへスナップグリッドを表示
	if (scenePanel && context.editorState) {

		context.editorState->assetDragSnapGridActive = preview_.IsActive() && context.editorState->enableSnapEditEntity;
		context.editorState->assetDragSnapGridIs3D = dropPreviewIsThreeD_;
	}
}

void Engine::ViewportPlacementSession::ApplyDropSnap(
	const EditorPanelContext& context, Vector3& position, bool isThreeD) const {

	// 現在の設定で配置スナップを適用
	if (!context.editorState || !context.editorState->enableSnapEditEntity) {
		return;
	}
	const EntitySnapSettings& snap = context.editorState->snapSettings;
	const float size = isThreeD ? snap.translate3D.size : snap.translate2D.size;
	if (size <= 0.0f) {
		return;
	}
	// 最寄りのグリッド線へ丸める
	auto snapAxis = [size](float value) { return std::round(value / size) * size; };
	position.x = snapAxis(position.x);
	position.y = snapAxis(position.y);
	position.z = snapAxis(position.z);
}

Engine::Vector3 Engine::ViewportPlacementSession::ComputeDropPosition(const EditorPanelContext& context,
	RenderViewKind viewKind, bool isThreeD, const ImVec2& imagePos, uint32_t renderWidth, uint32_t renderHeight) const {

	const ImVec2 mouse = ImGui::GetMousePos();
	float nx = (viewSize_.x > 0.0f) ? (mouse.x - imagePos.x) / viewSize_.x : 0.5f;
	float ny = (viewSize_.y > 0.0f) ? (mouse.y - imagePos.y) / viewSize_.y : 0.5f;
	nx = std::clamp(nx, 0.0f, 1.0f);
	ny = std::clamp(ny, 0.0f, 1.0f);

	// 2Dは画面のピクセル空間に置く、正射影は左上原点のピクセル基準
	if (!isThreeD) {
		return Vector3(nx * static_cast<float>(renderWidth), ny * static_cast<float>(renderHeight), 0.0f);
	}

	// 3DはCameraの光線と地面の交点へ置く
	if (!context.renderPipeline) {
		return Vector3::AnyInit(0.0f);
	}
	const ResolvedRenderView& view = context.renderPipeline->GetResolvedView(viewKind);
	const ResolvedCameraView& camera = view.perspective;
	if (!camera.valid) {
		return Vector3::AnyInit(0.0f);
	}
	const Matrix4x4 invViewProj = camera.matrices.inverseProjectionMatrix * camera.matrices.inverseViewMatrix;
	const float ndcX = nx * 2.0f - 1.0f;
	const float ndcY = 1.0f - ny * 2.0f;
	const Vector3 nearPoint = Vector3::Transform(Vector3(ndcX, ndcY, 0.0f), invViewProj);
	const Vector3 farPoint = Vector3::Transform(Vector3(ndcX, ndcY, 1.0f), invViewProj);
	const Vector3 direction = Vector3::Normalize(farPoint - nearPoint);
	// 平行投影では画面上の位置から光線を出す
	const Vector3 origin = camera.projectionMode == ResolvedProjectionMode::Orthographic ? nearPoint : camera.cameraPos;

	// 地面と交わるならその点、平行に近ければカメラ前方の一定距離へ置く
	if (std::abs(direction.y) > 1e-4f) {

		const float t = -origin.y / direction.y;
		if (t > 0.0f) {
			return origin + direction * t;
		}
	}
	return origin + direction * 10.0f;
}

void Engine::ViewportPlacementSession::DestroyDropPreview() {

	// 開始Worldが生存している仮Entityだけを破棄
	preview_.End();
	dropPreviewAsset_ = AssetID{};
}

void Engine::ViewportPlacementSession::EndPreview() {

	DestroyDropPreview();
	dropPreviewCanceled_ = false;
}
