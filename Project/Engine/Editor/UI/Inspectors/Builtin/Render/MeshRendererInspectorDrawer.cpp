#include "MeshRendererInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Common/MaterialParameterEditor.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

// c++
#include <filesystem>
#include <variant>

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
		// 色やテクスチャはシェーダーreflection駆動でマテリアルパラメータとして編集する
		DrawSubMeshReflectedParameters(context, GetDraft().material, subMesh, anyItemActive);
}

const Engine::ShaderReflectionInfo* Engine::MeshRendererInspectorDrawer::EnsureMaterialReflection(
	const EditorPanelContext& context, AssetID materialID) {

	if (!context.renderPipeline || !context.editorContext || !context.editorContext->assetDatabase) {
		return nullptr;
	}
	// マテリアルが変わったときだけファイルを読み直す
	if (!cachedMaterialValid_ || cachedMaterialID_ != materialID) {

		cachedMaterialValid_ = false;
		cachedMaterialID_ = materialID;
		cachedMaterial_ = MaterialAsset{};
		const std::filesystem::path path = context.editorContext->assetDatabase->ResolveFullPath(materialID);
		if (!path.empty()) {

			nlohmann::json data = JsonAdapter::Load(path.string(), false);
			cachedMaterialValid_ = FromJson(data, cachedMaterial_);
		}
	}
	if (!cachedMaterialValid_) {
		return nullptr;
	}
	const MaterialPassBinding* drawPass = FindPass(cachedMaterial_, MaterialPassKind::Draw);
	if (!drawPass || !drawPass->pipeline) {
		return nullptr;
	}
	return context.renderPipeline->FindPipelineGraphicsReflection(drawPass->pipeline);
}

void Engine::MeshRendererInspectorDrawer::DrawSubMeshReflectedParameters(
	const EditorPanelContext& context, AssetID materialID, SubMeshMaterial& subMesh, bool& anyItemActive) {

	const ShaderReflectionInfo* reflection = EnsureMaterialReflection(context, materialID);
	if (!reflection) {
		ImGui::TextDisabled("マテリアルのパラメータを取得できません");
		return;
	}

	const ShaderConstantBufferInfo* cb = nullptr;
	for (const ShaderConstantBufferInfo& candidate : reflection->constantBuffers) {
		if (candidate.name == "gMeshMaterialParameters") {
			cb = &candidate;
			break;
		}
	}
	if (!cb) {
		return;
	}

	ImGui::SeparatorText("シェーダーパラメータ");

	// pad用の詰め物paramはインスペクタに出さない
	auto isPaddingParam = [](const std::string& name) {
		return !name.empty() && (name.front() == '_' ||
			name.find("pad") != std::string::npos || name.find("Pad") != std::string::npos);
		};
	// テクスチャparamはuintのbindless indexだが編集はAssetID参照で行う
	auto isTextureParam = [](const std::string& name) {
		return name.find("Texture") != std::string::npos ||
			name.find("texture") != std::string::npos || name.find("Map") != std::string::npos;
		};
	// 既存値が無ければマテリアル既定値、それも無ければ型既定値を初期表示にする
	auto resolveValue = [&](const ShaderConstantBufferVariable& var) {
		auto it = subMesh.parameterOverrides.find(var.name);
		if (it != subMesh.parameterOverrides.end()) {
			return it->second;
		}
		auto defaultIt = cachedMaterial_.parameters.find(var.name);
		return defaultIt != cachedMaterial_.parameters.end() ?
			defaultIt->second : MaterialParameterEditor::DefaultValueForVariable(var);
		};
	// paramごとのRangeとDragValueを返す、必要なものだけ個別設定する
	auto resolveFloatSetting = [](const std::string& name) {
		Engine::FloatEditSetting setting{};
		if (name == "Metallic" || name == "Roughness") {
			setting.dragSpeed = 0.01f;
			setting.minValue = 0.0f;
			setting.maxValue = 1.0f;
		}
		return setting;
		};

	// Drag編集paramを先に出す
	for (const ShaderConstantBufferVariable& var : cb->variables) {

		if (isPaddingParam(var.name) || isTextureParam(var.name)) {
			continue;
		}
		MaterialParameterValue value = resolveValue(var);
		const Engine::FloatEditSetting floatSetting = resolveFloatSetting(var.name);
		DrawField(anyItemActive, [&]() {

			const bool changed = MaterialParameterEditor::DrawValueEdit(var, value, floatSetting);
			if (changed) {
				subMesh.parameterOverrides[var.name] = value;
			}
			Engine::ValueEditResult editResult{};
			editResult.valueChanged = changed;
			return editResult;
			});
	}

	// テクスチャparamは下にまとめて出す
	for (const ShaderConstantBufferVariable& var : cb->variables) {

		if (isPaddingParam(var.name) || !isTextureParam(var.name)) {
			continue;
		}
		auto it = subMesh.parameterOverrides.find(var.name);
		AssetID textureID{};
		if (it != subMesh.parameterOverrides.end() && std::holds_alternative<AssetID>(it->second.value)) {
			textureID = std::get<AssetID>(it->second.value);
		}
		DrawField(anyItemActive, [&]() {

			AssetEditSetting setting{};
			setting.graphicsCore = context.graphicsCore;
			auto result = MyGUI::AssetReferenceField(var.name.c_str(), textureID,
				context.editorContext->assetDatabase, { AssetType::Texture }, setting);
			if (result.valueChanged) {
				MaterialParameterValue value{};
				value.value = textureID;
				subMesh.parameterOverrides[var.name] = value;
			}
			return result;
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
