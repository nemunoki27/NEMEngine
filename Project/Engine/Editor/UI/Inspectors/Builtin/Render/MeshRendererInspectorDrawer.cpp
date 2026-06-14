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
#include <cstring>
#include <filesystem>
#include <optional>
#include <variant>

//============================================================================
//	MeshRendererInspectorDrawer internal
//============================================================================
namespace {

	// 2つのパラメータ値が同じ型かつ同じ値か、サブメッシュ間の混在判定に使う
	bool ParamValueEqual(const Engine::MaterialParameterValue& a, const Engine::MaterialParameterValue& b) {

		if (a.value.index() != b.value.index()) {
			return false;
		}
		return std::visit([&](const auto& av) {
			using T = std::decay_t<decltype(av)>;
			const T& bv = std::get<T>(b.value);
			return std::memcmp(&av, &bv, sizeof(T)) == 0;
			}, a.value);
	}

	// paramごとのRangeとDragValueを返す、必要なものだけ個別設定する
	Engine::FloatEditSetting MakeReflectedFloatSetting(const std::string& name) {

		Engine::FloatEditSetting setting{};
		if (name == "Metallic" || name == "Roughness") {
			setting.dragSpeed = 0.01f;
			setting.minValue = 0.0f;
			setting.maxValue = 1.0f;
		}
		return setting;
	}

	// 名前からテクスチャparamか、padding詰め物paramかを判定する
	bool IsReflectedTextureParam(const std::string& name) {
		return name.find("Texture") != std::string::npos ||
			name.find("texture") != std::string::npos || name.find("Map") != std::string::npos;
	}
	bool IsReflectedPaddingParam(const std::string& name) {
		return !name.empty() && (name.front() == '_' ||
			name.find("pad") != std::string::npos || name.find("Pad") != std::string::npos);
	}

	// モデル側サブメッシュ材質から、指定paramへ入れるべき値を返す、設定が無ければnullopt
	std::optional<Engine::MaterialParameterValue> ResolveModelParamValue(
		const std::string& name, const Engine::MeshSubMeshLayoutItem& item) {

		using Value = Engine::MaterialParameterValue;
		auto color = [](const Engine::Color4& c) { Value v{}; v.value = c; return v; };
		auto number = [](float f) { Value v{}; v.value = f; return v; };
		auto texture = [](const Engine::AssetID& id) { Value v{}; v.value = id; return v; };

		if (name == "color" && item.hasBaseColorFactor) { return color(item.baseColorFactor); }
		if (name == "emissiveColor" && item.hasEmissiveFactor) { return color(item.emissiveFactor); }
		if (name == "Metallic" && item.hasMetallicFactor) { return number(item.metallicFactor); }
		if (name == "Roughness" && item.hasRoughnessFactor) { return number(item.roughnessFactor); }

		const auto& tex = item.defaultTextureAssets;
		if (name == "baseColorTexture" && tex.baseColorTexture) { return texture(tex.baseColorTexture); }
		if (name == "normalTexture" && tex.normalTexture) { return texture(tex.normalTexture); }
		if (name == "emissiveTexture" && tex.emissiveTexture) { return texture(tex.emissiveTexture); }
		if (name == "metallicRoughnessTexture" && tex.metallicRoughnessTexture) { return texture(tex.metallicRoughnessTexture); }
		if (name == "occlusionTexture" && tex.occlusionTexture) { return texture(tex.occlusionTexture); }
		if (name == "specularTexture" && tex.specularTexture) { return texture(tex.specularTexture); }
		return std::nullopt;
	}
}

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

	// 一番下に全サブメッシュ同時編集UIを置く
	DrawBatchSubMeshMaterialEditor(context, draft, anyItemActive);
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

void Engine::MeshRendererInspectorDrawer::ApplyModelMaterialParameters(
	const EditorPanelContext& context, MeshRendererComponent& draft) {

	AssetDatabase* assetDatabase = context.editorContext ? context.editorContext->assetDatabase : nullptr;
	if (!assetDatabase) {
		return;
	}
	// モデルファイルから最新のサブメッシュ材質を読み直す
	std::vector<MeshSubMeshLayoutItem> layout;
	if (!MeshSubMeshAuthoring::TryBuildLayout(assetDatabase, draft.mesh, layout)) {
		return;
	}
	// 現shaderのMaterialParametersに存在する項目だけ適用する
	const ShaderReflectionInfo* reflection = EnsureMaterialReflection(context, draft.material);
	if (!reflection) {
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

	for (SubMeshMaterial& subMesh : draft.subMeshes) {

		// モデル側の対応サブメッシュを元インデックスで探す
		const MeshSubMeshLayoutItem* item = nullptr;
		for (const MeshSubMeshLayoutItem& layoutItem : layout) {
			if (layoutItem.sourceSubMeshIndex == subMesh.sourceSubMeshIndex) {
				item = &layoutItem;
				break;
			}
		}
		if (!item) {
			continue;
		}
		for (const ShaderConstantBufferVariable& var : cb->variables) {

			auto value = ResolveModelParamValue(var.name, *item);
			if (value) {
				subMesh.parameterOverrides[var.name] = *value;
			}
		}
	}
}

void Engine::MeshRendererInspectorDrawer::DrawBatchSubMeshMaterialEditor(
	const EditorPanelContext& context, MeshRendererComponent& draft, bool& anyItemActive) {

	ImGui::SeparatorText("マテリアル同時編集");

	// 同時編集はエディタのUI状態でコンポーネントには保存しない
	MyGUI::Checkbox("同時編集", batchEditSubMeshMaterials_);
	if (!batchEditSubMeshMaterials_) {
		batchOverrideAllowed_.clear();
		return;
	}
	if (draft.subMeshes.empty()) {
		return;
	}

	const ShaderReflectionInfo* reflection = EnsureMaterialReflection(context, draft.material);
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

	// サブメッシュ1件分の最終値を返す、上書き無しはマテリアル既定値か型既定値
	auto resolveForSubMesh = [&](const SubMeshMaterial& subMesh, const ShaderConstantBufferVariable& var) {
		auto it = subMesh.parameterOverrides.find(var.name);
		if (it != subMesh.parameterOverrides.end()) {
			return it->second;
		}
		auto defaultIt = cachedMaterial_.parameters.find(var.name);
		return defaultIt != cachedMaterial_.parameters.end() ?
			defaultIt->second : MaterialParameterEditor::DefaultValueForVariable(var);
		};
	// 編集確定値を全サブメッシュへ書き込む
	auto applyToAll = [&](const std::string& name, const MaterialParameterValue& value) {
		for (SubMeshMaterial& subMesh : draft.subMeshes) {
			subMesh.parameterOverrides[name] = value;
		}
		};

	for (const ShaderConstantBufferVariable& var : cb->variables) {

		if (IsReflectedPaddingParam(var.name)) {
			continue;
		}

		// 全サブメッシュで値が一致しているか調べ、混在していれば既定では編集無効にする
		MaterialParameterValue common = resolveForSubMesh(draft.subMeshes.front(), var);
		bool mixed = false;
		for (size_t i = 1; i < draft.subMeshes.size(); ++i) {
			if (!ParamValueEqual(resolveForSubMesh(draft.subMeshes[i], var), common)) {
				mixed = true;
				break;
			}
		}
		const bool allowed = batchOverrideAllowed_.count(var.name) != 0;

		// 混在かつ未許可は - 表示で無効、右に上書き許可ボタンを置く
		if (mixed && !allowed) {

			if (MyGUI::BeginPropertyRow(var.name.c_str())) {

				const float spacing = ImGui::GetStyle().ItemSpacing.x;
				const float buttonWidth = ImGui::CalcTextSize("上書きを許可").x + ImGui::GetStyle().FramePadding.x * 2.0f;
				const float dashWidth = (std::max)(1.0f, ImGui::GetContentRegionAvail().x - buttonWidth - spacing);
				ImGui::BeginDisabled();
				ImGui::Button((std::string("-##batchdash_") + var.name).c_str(), ImVec2(dashWidth, 0.0f));
				ImGui::EndDisabled();
				ImGui::SameLine();
				if (ImGui::Button((std::string("上書きを許可##batch_") + var.name).c_str(), ImVec2(buttonWidth, 0.0f))) {
					batchOverrideAllowed_.insert(var.name);
				}
				MyGUI::EndPropertyRow();
			}
			continue;
		}

		// 一致または許可済みは通常編集、変更を全サブメッシュへ適用する
		if (IsReflectedTextureParam(var.name)) {

			AssetID textureID{};
			if (std::holds_alternative<AssetID>(common.value)) {
				textureID = std::get<AssetID>(common.value);
			}
			DrawField(anyItemActive, [&]() {

				ValueEditResult result{};
				AssetEditSetting setting{};
				setting.graphicsCore = context.graphicsCore;
				auto fieldResult = MyGUI::AssetReferenceField(var.name.c_str(), textureID,
					context.editorContext->assetDatabase, { AssetType::Texture }, setting);
				if (fieldResult.valueChanged) {

					MaterialParameterValue value{};
					value.value = textureID;
					applyToAll(var.name, value);
					result.valueChanged = true;
					result.editFinished = true;
				}
				return result;
				});
		} else {

			MaterialParameterValue value = common;
			const Engine::FloatEditSetting floatSetting = MakeReflectedFloatSetting(var.name);
			DrawField(anyItemActive, [&]() {

				ValueEditResult result{};
				if (MaterialParameterEditor::DrawValueEdit(var, value, floatSetting)) {
					applyToAll(var.name, value);
					result.valueChanged = true;
					result.editFinished = true;
				}
				return result;
				});
		}
	}
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
