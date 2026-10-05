#include "MeshRendererInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Materials/DefaultMaterialSettings.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

//============================================================================
//	MeshRendererInspectorDrawer classMethods
//============================================================================
void Engine::MeshRendererInspectorDrawer::DrawFields(const EditorPanelContext& context, ECSWorld& world,
	const Entity& entity, bool& anyItemActive) {

	// ドラフトコンポーネントを参照
	auto& draft = GetDraft();
	previewSubMeshIndex_ = UINT32_MAX;

	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("表示", draft.visible); });

	// 現在のメッシュに合わせてサブメッシュ配列を整える
	SyncDraftSubMeshes(context, draft, true);

	// サブメッシュが選択されている場合はサブメッシュのフィールドを表示する
	uint32_t selectedSubMeshIndex = 0;
	if (TryGetSelectedSubMeshIndex(context, world, entity, draft, selectedSubMeshIndex)) {

		previewSubMeshIndex_ = selectedSubMeshIndex;
		auto& subMesh = subMeshDraft_[selectedSubMeshIndex];
		DrawSubMeshFields(context, world, entity, subMesh, anyItemActive);
		return;
	}

	//============================================================================
	//	アセットファイル
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			ValueEditResult result =
				MyGUI::AssetReferenceField("メッシュ", draft.mesh, context.editorContext->assetDatabase, {AssetType::Mesh});
			// メッシュが変更されたらサブメッシュのリストを更新する
			if (result.valueChanged) {

				SyncDraftSubMeshes(context, draft, false);
				result.editFinished = true;
			}
			return result;
		});
		DrawField(anyItemActive, [&]() {
			AssetEditSetting setting{};
			setting.defaultAssetID = DefaultMaterialSettings::GetInstance().GetMeshOrBuiltin();
			return MyGUI::AssetReferenceField(
				"既定マテリアル", draft.material, context.editorContext->assetDatabase, {AssetType::Material}, setting);
		});

		// モデルファイルのマテリアル係数やテクスチャを現shaderのパラメータへ再適用する
		DrawField(anyItemActive, [&]() {
			ValueEditResult result{};
			const float fullWidth = ImGui::GetContentRegionAvail().x;
			const float buttonWidth = fullWidth * 0.5f;
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (fullWidth - buttonWidth) * 0.5f);
			if (ImGui::Button("マテリアルパラメータを再読み込み", ImVec2(buttonWidth, 0.0f))) {

				ApplyModelMaterialParameters(context, draft);
				result.valueChanged = true;
				result.editFinished = true;
			}
			return result;
		});
		DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("Zプリパス", draft.enableZPrepass); });
	}

	//============================================================================
	//	メッシュ描画パラメータ
	//============================================================================
	{
		DrawField(anyItemActive, [&]() { return MyGUI::DragInt("レイヤー", draft.layer); });
		DrawField(anyItemActive, [&]() { return MyGUI::DragInt("描画順", draft.order); });
		DrawField(
			anyItemActive, [&]() { return InspectorDrawerCommon::DrawEnumComboField("既定ブレンドモード", draft.blendMode); });
		DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawEnumComboField("既定キュー", draft.queue); });
		DrawField(anyItemActive,
			[&]() { return InspectorDrawerCommon::DrawLayerMaskField(context, "Rendering Layer", draft.renderingLayerMask); });

		// ライティングや影の適用フラグ、ビット単位で持つためboolへ写してから書き戻す
		auto drawRenderFlagField = [&](const char* label, MeshRenderFlags flag) {
			DrawField(anyItemActive, [&]() {
				bool enabled = HasMeshRenderFlag(draft.renderFlags, flag);
				ValueEditResult result = InspectorDrawerCommon::DrawCheckboxField(label, enabled);
				if (result.valueChanged) {

					SetMeshRenderFlag(draft.renderFlags, flag, enabled);
				}
				return result;
			});
		};
		drawRenderFlagField("ライティング", MeshRenderFlags::Lighting);
		drawRenderFlagField("影を落とす", MeshRenderFlags::CastShadow);
		drawRenderFlagField("影を受ける", MeshRenderFlags::ReceiveShadow);
		drawRenderFlagField("IBLを受ける", MeshRenderFlags::ReceiveIBL);
		drawRenderFlagField("反射に映す", MeshRenderFlags::CastReflection);
		drawRenderFlagField("反射を受ける", MeshRenderFlags::ReceiveReflection);
	}

	// 一番下に全サブメッシュ同時編集UIを置く
	PushEditResult(materialEditor_.DrawBatch(context, draft, subMeshDraft_), anyItemActive);
}

void Engine::MeshRendererInspectorDrawer::ApplyPreview(
	ECSWorld& world, const Entity& entity, const MeshRendererComponent& previewComponent) {

	if (!world.IsAlive(entity) || !world.HasComponent<MeshRendererComponent>(entity)) {
		return;
	}

	if (previewSubMeshIndex_ < subMeshDraft_.size() &&
		SetMeshSubMesh(world, entity, previewSubMeshIndex_, subMeshDraft_[previewSubMeshIndex_])) {
		return;
	}

	world.GetComponent<MeshRendererComponent>(entity) = previewComponent;
	SetMeshSubMeshes(world, entity, subMeshDraft_);
	world.MarkComponentModified<MeshRendererComponent>(entity);
}

void Engine::MeshRendererInspectorDrawer::OnSyncDraftFromWorld(
	ECSWorld& world, const Entity& entity, [[maybe_unused]] const MeshRendererComponent& component) {

	const std::span<const SubMeshMaterial> subMeshes = GetMeshSubMeshes(world, entity);
	subMeshDraft_.assign(subMeshes.begin(), subMeshes.end());
}

void Engine::MeshRendererInspectorDrawer::SerializeDraft([[maybe_unused]] ECSWorld& world,
	[[maybe_unused]] const Entity& entity, const MeshRendererComponent& component, nlohmann::json& out) const {

	SerializeMeshRenderer(component, subMeshDraft_, out);
}

void Engine::MeshRendererInspectorDrawer::RefreshSubMeshLayoutCache(
	Engine::AssetDatabase* assetDatabase, Engine::AssetID meshAssetID) {

	const auto lifetime = assetDatabase ? assetDatabase->GetCacheLifetime() : std::weak_ptr<const uint8_t>{};
	const uint64_t revision = assetDatabase ? assetDatabase->GetStructureRevision() : 0;
	const uint64_t contentRevision = assetDatabase ? assetDatabase->GetContentRevision(meshAssetID) : 0;
	if (cachedMeshAssetID_ == meshAssetID && !cachedDatabaseLifetime_.expired() &&
		!cachedDatabaseLifetime_.owner_before(lifetime) && !lifetime.owner_before(cachedDatabaseLifetime_) &&
		cachedDatabaseRevision_ == revision && cachedMeshContentRevision_ == contentRevision) {
		return;
	}
	// 同じMeshの再読込とProject切替も反映する
	cachedMeshAssetID_ = meshAssetID;
	cachedDatabaseLifetime_ = lifetime;
	cachedMeshContentRevision_ = contentRevision;
	cachedSubMeshLayout_.clear();
	// キャッシュを更新
	cachedSubMeshLayoutResolved_ = MeshSubMeshAuthoring::TryBuildLayout(assetDatabase, meshAssetID, cachedSubMeshLayout_);
	cachedDatabaseRevision_ = assetDatabase ? assetDatabase->GetStructureRevision() : 0;
}

void Engine::MeshRendererInspectorDrawer::SyncDraftSubMeshes(
	const EditorPanelContext& context, MeshRendererComponent& draft, bool preserveOverrides) {

	if (!context.editorContext || !context.editorContext->assetDatabase) {
		return;
	}

	AssetDatabase* assetDatabase = context.editorContext->assetDatabase;

	// メッシュアセットの変更を検知してキャッシュを更新する
	RefreshSubMeshLayoutCache(assetDatabase, draft.mesh);

	if (!draft.mesh) {
		if (!subMeshDraft_.empty()) {
			subMeshDraft_.clear();
		}
		return;
	}

	// レイアウト未解決なら既存内容は壊さない
	if (!cachedSubMeshLayoutResolved_) {
		return;
	}

	// レイアウトに合わせてドラフトのサブメッシュを正規化する
	MeshSubMeshAuthoring::SyncComponentToLayout(cachedSubMeshLayout_, subMeshDraft_, preserveOverrides);
}

bool Engine::MeshRendererInspectorDrawer::TryGetSelectedSubMeshIndex(const EditorPanelContext& context, ECSWorld& world,
	const Entity& entity, [[maybe_unused]] const MeshRendererComponent& draft, uint32_t& outSubMeshIndex) const {

	// サブメッシュが選択されていることを前提に、選択されているサブメッシュのインデックスを返す
	if (!context.editorState || !context.editorState->HasValidSubMeshSelection(&world)) {
		return false;
	}
	if (context.editorState->selectedEntity != entity) {
		return false;
	}
	if (!context.editorState->TryResolveSelectedSubMeshIndex(&world, outSubMeshIndex)) {
		return false;
	}
	return outSubMeshIndex < subMeshDraft_.size();
}

void Engine::MeshRendererInspectorDrawer::DrawSubMeshFields(const EditorPanelContext& context, ECSWorld& world,
	const Entity& entity, SubMeshMaterial& subMesh, bool& anyItemActive) {

	PushEditResult(materialEditor_.DrawMaterialFields(context, GetDraft(), subMesh), anyItemActive);
	ImGui::Separator();

	// パラメータ
	{
		// ローカル変換
		{
			FloatEditSetting editSetting{
				.minValue = -100000.0f,
				.maxValue = 100000.0f,
				.closeOnProperty = false,
				.reserveRightWidth = 80.0f,
			};
			ImVec2 resetButtonSize = ImVec2(editSetting.reserveRightWidth, ImGui::GetFrameHeight());

			DrawField(anyItemActive, [&]() {
				editSetting.dragSpeed = 0.01f;
				auto result = MyGUI::DragVector3("ローカル位置", subMesh.localPos, editSetting);
				ImGui::SameLine();
				if (ImGui::Button("リセット##SubMeshLocalPos", resetButtonSize)) {
					subMesh.localPos.Init();
					result.valueChanged = true;
					result.editFinished = true;
				}
				MyGUI::EndPropertyRow();
				return result;
			});
			DrawField(anyItemActive, [&]() {
				editSetting.dragSpeed = 0.1f;
				auto result = MyGUI::DragVector3("ローカル回転", subMesh.localRotation, editSetting);
				ImGui::SameLine();
				if (ImGui::Button("リセット##SubMeshLocalRotation", resetButtonSize)) {
					subMesh.localRotation.Init();
					result.valueChanged = true;
					result.editFinished = true;
				}
				MyGUI::EndPropertyRow();
				return result;
			});
			DrawField(anyItemActive, [&]() {
				editSetting.dragSpeed = 0.01f;
				editSetting.minValue = 0.0f;
				auto result = MyGUI::DragVector3("ローカルスケール", subMesh.localScale, editSetting);
				ImGui::SameLine();
				if (ImGui::Button("リセット##SubMeshLocalScale", resetButtonSize)) {
					subMesh.localScale = Vector3::AnyInit(1.0f);
					result.valueChanged = true;
					result.editFinished = true;
				}
				MyGUI::EndPropertyRow();
				return result;
			});
		}
		ImGui::Separator();
		Matrix4x4 parentWorld = Matrix4x4::Identity();
		if (world.HasComponent<TransformComponent>(entity)) {
			parentWorld = world.GetComponent<TransformComponent>(entity).worldMatrix;
		}
		const Matrix4x4 worldMatrix = MeshSubMeshRuntime::BuildRenderLocalMatrix(subMesh) * parentWorld;
		MyGUI::TextMatrix4x4("ワールド行列", worldMatrix);
		ImGui::Separator();

		// UV
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragVector2(
				"UV位置", subMesh.uvPos, {.dragSpeed = 0.01f, .minValue = -100000.0f, .maxValue = 100000.0f});
		});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat(
				"UV回転", subMesh.uvRotation, {.dragSpeed = 0.01f, .minValue = -100000.0f, .maxValue = 100000.0f});
		});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragVector2(
				"UVスケール", subMesh.uvScale, {.dragSpeed = 0.01f, .minValue = -100000.0f, .maxValue = 100000.0f});
		});
		const Matrix4x4 uvMatrix = MeshSubMeshRuntime::BuildUVMatrix(subMesh);
		MyGUI::TextMatrix4x4("UV行列", uvMatrix);
		ImGui::Separator();
	}

	// 色やテクスチャはシェーダーreflection駆動でマテリアルパラメータとして編集する
	const AssetID materialID = subMesh.material ? subMesh.material : GetDraft().material;
	PushEditResult(materialEditor_.DrawParameters(context, materialID, subMesh), anyItemActive);
}

void Engine::MeshRendererInspectorDrawer::ApplyModelMaterialParameters(
	const EditorPanelContext& context, MeshRendererComponent& draft) {

	AssetDatabase* assetDatabase = context.editorContext ? context.editorContext->assetDatabase : nullptr;
	if (!assetDatabase) {
		return;
	}
	// モデルファイルから最新のサブメッシュ材質を読み直し、係数とテクスチャをオーサリング側で再適用する
	std::vector<MeshSubMeshLayoutItem> layout;
	if (!MeshSubMeshAuthoring::TryBuildLayout(assetDatabase, draft.mesh, layout)) {
		return;
	}
	MeshSubMeshAuthoring::ApplyModelMaterialParameters(layout, subMeshDraft_);
}
