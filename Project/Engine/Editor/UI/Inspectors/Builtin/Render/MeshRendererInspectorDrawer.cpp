#include "MeshRendererInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>

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
			ValueEditResult result = MyGUI::AssetReferenceField("メッシュ", draft.mesh,
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
			return MyGUI::AssetReferenceField("マテリアル", draft.material,
				context.editorContext->assetDatabase, { AssetType::Material });
			});
	}
	//============================================================================
	//	メッシュ描画パラメータ
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragInt("レイヤー", draft.layer);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragInt("描画順", draft.order);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("表示", draft.visible);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("Zプリパス", draft.enableZPrepass);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawEnumComboField("ブレンドモード", draft.blendMode);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawEnumComboField("キュー", draft.queue);
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
	Engine::AssetDatabase* assetDatabase, Engine::AssetID meshAssetID) {

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

	AssetDatabase* assetDatabase = context.editorContext->assetDatabase;

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
	SubMeshMaterial& subMesh, bool& anyItemActive) {

	// パラメータ
		{
			// ローカル変換
			{
				FloatEditSetting editSetting{ .minValue = -100000.0f, .maxValue = 100000.0f, .closeOnProperty = false, .reserveRightWidth = 80.0f };
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
			MyGUI::TextMatrix4x4("ワールド行列", subMesh.worldMatrix);
			ImGui::Separator();
			// 色
			DrawField(anyItemActive, [&]() {
				return MyGUI::ColorEdit("色", subMesh.color);
				});
			DrawField(anyItemActive, [&]() {
				return MyGUI::ColorEdit("発光色", subMesh.emissiveColor);
				});
			// PBRパラメータ
			DrawField(anyItemActive, [&]() {
				return MyGUI::DragFloat("メタリック", subMesh.metallic,
					{ .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1.0f });
				});
			DrawField(anyItemActive, [&]() {
				return MyGUI::DragFloat("ラフネス", subMesh.roughness,
					{ .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1.0f });
				});
			// UV
			DrawField(anyItemActive, [&]() {
				return MyGUI::DragVector2("UV位置", subMesh.uvPos,
					{ .dragSpeed = 0.01f, .minValue = -100000.0f, .maxValue = 100000.0f });
				});
			DrawField(anyItemActive, [&]() {
				return MyGUI::DragFloat("UV回転", subMesh.uvRotation,
					{ .dragSpeed = 0.01f, .minValue = -100000.0f, .maxValue = 100000.0f });
				});
			DrawField(anyItemActive, [&]() {
				return MyGUI::DragVector2("UVスケール", subMesh.uvScale,
					{ .dragSpeed = 0.01f, .minValue = -100000.0f, .maxValue = 100000.0f });
				});
			MyGUI::TextMatrix4x4("UV行列", subMesh.uvMatrix);
			ImGui::Separator();
		}
	// サブメッシュのテクスチャ設定
		{
			auto DrawTextureField = [&](const char* label, AssetID& textureID) {

				DrawField(anyItemActive, [&]() {

					// テクスチャプレビュー(ツールチップ)と右クリック削除はAssetReferenceFieldの共通機能で処理する
					AssetEditSetting setting{};
					setting.graphicsCore = context.graphicsCore;

					return MyGUI::AssetReferenceField(label, textureID,
						context.editorContext->assetDatabase, { AssetType::Texture }, setting);
					});
				};

			DrawTextureField("ベース色", subMesh.baseColorTexture);
			DrawTextureField("法線", subMesh.normalTexture);
			DrawTextureField("メタリック/ラフネス", subMesh.metallicRoughnessTexture);
			DrawTextureField("スペキュラ", subMesh.specularTexture);
			DrawTextureField("発光", subMesh.emissiveTexture);
			DrawTextureField("遮蔽(AO)", subMesh.occlusionTexture);
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
