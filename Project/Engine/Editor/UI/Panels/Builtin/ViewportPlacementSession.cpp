#include "ViewportPlacementSession.h"
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
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
	const ImVec2& imagePos, uint32_t renderWidth, uint32_t renderHeight, [[maybe_unused]] bool imageHovered, const ImVec2& imageSize, bool scenePanel) {

	viewSize_ = imageSize;

	ECSWorld* world = context.GetWorld();
	AssetDatabase* database = context.editorContext ? context.editorContext->assetDatabase : nullptr;

	// ドラッグ中はIsItemHoveredがアクティブアイテムにブロックされてfalseになるため、矩形内判定で重なりを見る
	const ImVec2 mousePos = ImGui::GetMousePos();
	const bool overImage = mousePos.x >= imagePos.x && mousePos.x <= imagePos.x + viewSize_.x &&
		mousePos.y >= imagePos.y && mousePos.y <= imagePos.y + viewSize_.y;

	// 現在ドラッグ中のプロジェクトアセットを覗き見る、ドロップ前でも参照できる
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

	// プレビューを出してよい条件、ビュー上をドラッグ中で配置可能なアセットのとき
	const bool canPreview = draggingAsset && overImage && !dropPreviewCanceled_ && assetPayload &&
		world && database && context.graphicsCore && context.CanEditScene() &&
		AssetEntityFactory::CanSpawn(*assetPayload);

	if (canPreview) {

		// アセットが変わった、または別ワールドのときは作り直す
		if (!dropPreviewActive_ || dropPreviewAsset_ != assetPayload->assetID || dropPreviewWorld_ != world) {

			DestroyDropPreview();
			HierarchySystem hierarchySystem{};
			const AssetSpawnResult spawn = AssetEntityFactory::Spawn(*world, *database, *context.graphicsCore,
				hierarchySystem, *assetPayload, context.editorContext->activeSceneInstanceID);
			if (spawn.valid) {

				dropPreviewEntity_ = spawn.root;
				dropPreviewIsThreeD_ = spawn.isThreeD;
				dropPreviewActive_ = true;
				dropPreviewAsset_ = assetPayload->assetID;
				dropPreviewWorld_ = world;
			}
		}
		// プレビュー位置を毎フレーム更新する、非同期ロードは描画側に任せ準備でき次第表示される
		if (dropPreviewActive_ && world->IsAlive(dropPreviewEntity_) &&
			world->HasComponent<TransformComponent>(dropPreviewEntity_)) {

			Vector3 position = ComputeDropPosition(context, viewKind, dropPreviewIsThreeD_, imagePos, renderWidth, renderHeight);
			ApplyDropSnap(context, position, dropPreviewIsThreeD_);
			auto& transform = world->GetComponent<TransformComponent>(dropPreviewEntity_);
			transform.localPos = position;
			MarkTransformSubtreeDirty(*world, dropPreviewEntity_);
		}
	} else if (dropPreviewActive_) {

		// ビュー外/キャンセル/ドラッグ終了でプレビューを片付ける
		DestroyDropPreview();
	}

	// ドロップ確定、Imageの上で離されたときだけ受理する
	if (ImGui::BeginDragDropTarget()) {

		if (const ImGuiPayload* accepted =
			ImGui::AcceptDragDropPayload(IEditorPanel::kProjectAssetDragDropPayloadType)) {

			if (!dropPreviewCanceled_ && dropPreviewActive_ && world && world->IsAlive(dropPreviewEntity_)) {

				// プレビューをそのまま確定して選択する、破棄対象から外す
				if (context.editorState) {
					context.editorState->SelectEntity(dropPreviewEntity_);
				}
				// 作成済みエンティティをUndo/Redo対象として履歴へ登録する
				if (context.host) {
					context.host->ExecuteEditorCommand(std::make_unique<CreateDroppedEntityCommand>(dropPreviewEntity_));
				}
				dropPreviewActive_ = false;
				dropPreviewEntity_ = Entity::Null();
				dropPreviewWorld_ = nullptr;
				dropPreviewAsset_ = AssetID{};
			}
		}
		ImGui::EndDragDropTarget();
	}

	// SceneViewでアセットをスナップ有効でドラッグ中なら、スナップグリッド表示を要求する
	if (scenePanel && context.editorState) {

		context.editorState->assetDragSnapGridActive = dropPreviewActive_ && context.editorState->enableSnapEditEntity;
		context.editorState->assetDragSnapGridIs3D = dropPreviewIsThreeD_;
	}
}

void Engine::ViewportPlacementSession::ApplyDropSnap(const EditorPanelContext& context, Vector3& position, bool isThreeD) const {

	// スナップ有効時は現在の座標スナップ設定の間隔へ吸着させる、表示しているスナップグリッドと一致させる
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

Engine::Vector3 Engine::ViewportPlacementSession::ComputeDropPosition(const EditorPanelContext& context, RenderViewKind viewKind,
	bool isThreeD, const ImVec2& imagePos, uint32_t renderWidth, uint32_t renderHeight) const {

	const ImVec2 mouse = ImGui::GetMousePos();
	float nx = (viewSize_.x > 0.0f) ? (mouse.x - imagePos.x) / viewSize_.x : 0.5f;
	float ny = (viewSize_.y > 0.0f) ? (mouse.y - imagePos.y) / viewSize_.y : 0.5f;
	nx = std::clamp(nx, 0.0f, 1.0f);
	ny = std::clamp(ny, 0.0f, 1.0f);

	// 2Dは画面のピクセル空間に置く、正射影は左上原点のピクセル基準
	if (!isThreeD) {
		return Vector3(nx * static_cast<float>(renderWidth), ny * static_cast<float>(renderHeight), 0.0f);
	}

	// 3Dは透視カメラ光線と地面Y=0平面の交点に置く
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
	const Vector3 origin = camera.cameraPos;

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

	if (!dropPreviewActive_) {
		return;
	}
	if (dropPreviewWorld_ && dropPreviewWorld_->IsAlive(dropPreviewEntity_)) {
		EditorEntitySnapshotUtility::DestroySubtree(*dropPreviewWorld_, dropPreviewEntity_);
	}
	dropPreviewActive_ = false;
	dropPreviewEntity_ = Entity::Null();
	dropPreviewWorld_ = nullptr;
	dropPreviewAsset_ = AssetID{};
}
