#include "MeshRendererInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Inspector/Methods/Common/InspectorDrawerCommon.h>
#include <Engine/Core/ECS/Component/Builtin/TransformComponent.h>
#include <Engine/Core/Asset/AssetDatabase.h> 
#include <Engine/Utility/ImGui/MyGUI.h>

//============================================================================
//	MeshRendererInspectorDrawer classMethods
//============================================================================

void Engine::MeshRendererInspectorDrawer::DrawFields(const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	// ドラフトコンポーネントを参照
	auto& draft = GetDraft();

	// 現在のメッシュに合わせてサブメッシュ配列を整える
	SyncDraftSubMeshes(context, draft, true);

	// サブメッシュが選択されている場合はサブメッシュのフィールドを表示する
	uint32_t selectedSubMeshIndex = 0;
	if (TryGetSelectedSubMeshIndex(context, world, entity, draft, selectedSubMeshIndex)) {

		Matrix4x4 parentWorld = Matrix4x4::Identity();
		if (world.HasComponent<TransformComponent>(entity)) {
			parentWorld = world.GetComponent<TransformComponent>(entity).worldMatrix;
		}
		if (selectedSubMeshIndex < draft.subMeshes.size()) {
			MeshSubMeshRuntime::UpdateSubMeshRuntime(draft.subMeshes[selectedSubMeshIndex], parentWorld);
		}
		auto& subMesh = draft.subMeshes[selectedSubMeshIndex];
		DrawSubMeshFields(context, world, entity, subMesh, anyItemActive);
		return;
	}

	//============================================================================
	//	アセットファイル
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			ValueEditResult result = MyGUI::AssetReferenceField("Mesh", draft.mesh,
				context.editorContext->assetDatabase, { AssetType::Mesh });
			// メッシュが変更されたらサブメッシュのリストを更新する
			if (result.valueChanged) {

				SyncDraftSubMeshes(context, draft, false);
				UpdateDraftRuntime(world, entity, draft);
				result.editFinished = true;
			}
			return result;
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::AssetReferenceField("Material", draft.material,
				context.editorContext->assetDatabase, { AssetType::Material });
			});
	}
	//============================================================================
	//	メッシュ描画パラメータ
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragInt("Layer", draft.layer);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragInt("Order", draft.order);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("Visible", draft.visible);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("ZPrepass", draft.enableZPrepass);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawEnumComboField("Blend Mode", draft.blendMode);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::InputText("Queue", draft.queue);
			});
	}
}

void Engine::MeshRendererInspectorDrawer::ApplyPreview(ECSWorld& world, const Entity& entity,
	const MeshRendererComponent& previewComponent) {

	if (!world.IsAlive(entity) || !world.HasComponent<MeshRendererComponent>(entity)) {
		return;
	}

	MeshRendererComponent preview = previewComponent;

	Matrix4x4 parentWorld = Matrix4x4::Identity();
	if (world.HasComponent<TransformComponent>(entity)) {
		parentWorld = world.GetComponent<TransformComponent>(entity).worldMatrix;
	}

	MeshSubMeshRuntime::UpdateRendererRuntime(preview, parentWorld);
	world.GetComponent<MeshRendererComponent>(entity) = std::move(preview);
}

void Engine::MeshRendererInspectorDrawer::RefreshSubMeshLayoutCache(
	const Engine::AssetDatabase* assetDatabase, Engine::AssetID meshAssetID) {

	if (cachedMeshAssetID_ == meshAssetID) {
		return;
	}
	cachedMeshAssetID_ = meshAssetID;
	cachedSubMeshLayout_.clear();
	// キャッシュを更新
	cachedSubMeshLayoutResolved_ = MeshSubMeshAuthoring::TryBuildLayout(assetDatabase, meshAssetID, cachedSubMeshLayout_);
}

void Engine::MeshRendererInspectorDrawer::SyncDraftSubMeshes(const EditorPanelContext& context,
	MeshRendererComponent& draft, bool preserveOverrides) {

	if (!context.editorContext || !context.editorContext->assetDatabase) {
		return;
	}

	const AssetDatabase* assetDatabase = context.editorContext->assetDatabase;

	// メッシュアセットの変更を検知してキャッシュを更新する
	RefreshSubMeshLayoutCache(assetDatabase, draft.mesh);

	if (!draft.mesh) {
		if (!draft.subMeshes.empty()) {
			draft.subMeshes.clear();
		}
		return;
	}

	// レイアウト未解決なら既存内容は壊さない
	if (!cachedSubMeshLayoutResolved_) {
		return;
	}

	// レイアウトに合わせてドラフトのサブメッシュを正規化する
	MeshSubMeshAuthoring::SyncComponentToLayout(cachedSubMeshLayout_, draft, preserveOverrides);
}

bool Engine::MeshRendererInspectorDrawer::TryGetSelectedSubMeshIndex(const EditorPanelContext& context,
	ECSWorld& world, const Entity& entity, const MeshRendererComponent& draft, uint32_t& outSubMeshIndex) const {

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
	return outSubMeshIndex < draft.subMeshes.size();
}

void Engine::MeshRendererInspectorDrawer::DrawSubMeshFields(const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	MeshSubMeshTextureOverride& subMesh, bool& anyItemActive) {

	// パラメータ
		{
			// ローカル変換
			DrawField(anyItemActive, [&]() {
				return MyGUI::DragVector3("Local Position", subMesh.localPos,
					{ .dragSpeed = 0.01f, .minValue = -100000.0f, .maxValue = 100000.0f });
				});
			DrawField(anyItemActive, [&]() {
				return MyGUI::DragVector3("Local Rotation", subMesh.localRotation,
					{ .dragSpeed = 0.1f, .minValue = -100000.0f, .maxValue = 100000.0f });
				});
			DrawField(anyItemActive, [&]() {
				return MyGUI::DragVector3("Local Scale", subMesh.localScale,
					{ .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 100000.0f });
				});
			MyGUI::TextVector3("Source Pivot", subMesh.sourcePivot);
			ImGui::Separator();
			MyGUI::TextMatrix4x4("World Matrix", subMesh.worldMatrix);
			ImGui::Separator();
			// 色
			DrawField(anyItemActive, [&]() {
				return MyGUI::ColorEdit("Color", subMesh.color);
				});
			// UV
			DrawField(anyItemActive, [&]() {
				return MyGUI::DragVector2("UV Position", subMesh.uvPos,
					{ .dragSpeed = 0.01f, .minValue = -100000.0f, .maxValue = 100000.0f });
				});
			DrawField(anyItemActive, [&]() {
				return MyGUI::DragFloat("UV Rotation", subMesh.uvRotation,
					{ .dragSpeed = 0.01f, .minValue = -100000.0f, .maxValue = 100000.0f });
				});
			DrawField(anyItemActive, [&]() {
				return MyGUI::DragVector2("UV Scale", subMesh.uvScale,
					{ .dragSpeed = 0.01f, .minValue = -100000.0f, .maxValue = 100000.0f });
				});
			MyGUI::TextMatrix4x4("UV Matrix", subMesh.uvMatrix);
			ImGui::Separator();
		}
	// サブメッシュのテクスチャ設定
		{
			DrawField(anyItemActive, [&]() {
				return MyGUI::AssetReferenceField("Base Color", subMesh.baseColorTexture,
					context.editorContext->assetDatabase, { AssetType::Texture });
				});
			DrawField(anyItemActive, [&]() {
				return MyGUI::AssetReferenceField("Normal", subMesh.normalTexture,
					context.editorContext->assetDatabase, { AssetType::Texture });
				});
			DrawField(anyItemActive, [&]() {
				return MyGUI::AssetReferenceField("Metallic", subMesh.metallicRoughnessTexture,
					context.editorContext->assetDatabase, { AssetType::Texture });
				});
			DrawField(anyItemActive, [&]() {
				return MyGUI::AssetReferenceField("Specular", subMesh.specularTexture,
					context.editorContext->assetDatabase, { AssetType::Texture });
				});
			DrawField(anyItemActive, [&]() {
				return MyGUI::AssetReferenceField("Emissive", subMesh.emissiveTexture,
					context.editorContext->assetDatabase, { AssetType::Texture });
				});
			DrawField(anyItemActive, [&]() {
				return MyGUI::AssetReferenceField("Occlusion", subMesh.occlusionTexture,
					context.editorContext->assetDatabase, { AssetType::Texture });
				});
		}
}

void Engine::MeshRendererInspectorDrawer::UpdateDraftRuntime(
	ECSWorld& world, const Entity& entity, MeshRendererComponent& draft) const {

	Matrix4x4 parentWorld = Matrix4x4::Identity();
	if (world.HasComponent<TransformComponent>(entity)) {

		parentWorld = world.GetComponent<TransformComponent>(entity).worldMatrix;
	}
	MeshSubMeshRuntime::UpdateRendererRuntime(draft, parentWorld);
}