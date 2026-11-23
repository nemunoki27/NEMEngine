#include "MaterialAssetFields.h"
#include <Engine/Editor/UI/Common/DecalMaterialFields.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Editor/UI/Common/MaterialParameterEditor.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

	Engine::ValueEditResult DrawPassReference(const Engine::EditorPanelContext& context, const char* label,
		Engine::AssetID& id, Engine::AssetType type) {

		const auto* database = context.editorContext->assetDatabase;
		Engine::AssetEditSetting setting{};
		const auto* meta = id ? database->Find(id) : nullptr;
		if (meta && meta->assetPath.starts_with("Engine/Assets/")) {
			setting.resolvedLabel = "エンジン: " + meta->assetPath.substr(meta->assetPath.find_last_of('/') + 1);
		} else if (id && !meta && context.renderPipeline) {
			auto& library = context.renderPipeline->GetRenderAssetLibrary();
			if (type == Engine::AssetType::RenderPipeline) {
				if (const auto* pipeline = library.LoadPipeline(id)) setting.resolvedLabel = "生成済み: " + pipeline->name;
			} else if (const auto* shader = library.LoadShader(id)) {
				setting.resolvedLabel = "生成済み: " + shader->name;
			}
		}
		if (!id && type == Engine::AssetType::Shader) setting.resolvedLabel = "パイプラインの既定";
		return Engine::MyGUI::AssetReferenceField(label, id, database, {type}, setting);
	}
}

//============================================================================
//	MaterialAssetFields functions
//============================================================================

namespace Engine::MaterialAssetFields {
namespace {

	void DrawShaderFields(const EditorPanelContext& context, MaterialAsset& draft, ValueEditResult& result,
		std::unordered_set<std::string>& reflectedNames) {

		const auto bufferName = draft.usage == MaterialUsage::Particle ?
			MaterialParameterCBuffer::kParticle : MaterialParameterCBuffer::kSurface;
		// 描画済みPipelineから公開パラメータを集める
		std::vector<const ShaderReflectionInfo*> reflections;
		ShaderReflectionInfo parameterVisibility;
		if (context.renderPipeline) {

			auto& library = context.renderPipeline->GetRenderAssetLibrary();
			const auto applyVisibility = [&](AssetID shaderID) {
				if (const auto* shader = shaderID ? library.LoadShader(shaderID) : nullptr) {
					ShaderReflectionInfo shaderVisibility;
					ApplyShaderParameterMetadata(shaderVisibility, *shader);
					MergeShaderReflection(parameterVisibility, shaderVisibility);
				}
			};
			std::unordered_set<const ShaderReflectionInfo*> seenReflections;
			for (const MaterialPassBinding& pass : draft.passes) {

				if (const auto* pipeline = pass.pipeline ? library.LoadPipeline(pass.pipeline) : nullptr) {
					for (const auto& variant : pipeline->variants) applyVisibility(variant.shader);
				}
				applyVisibility(pass.shaderOverride);
				if (!pass.pipeline) {
					continue;
				}
				MaterialAsset passMaterial{};
				passMaterial.passes.emplace_back(pass);
				const ShaderReflectionInfo* reflection = context.renderPipeline->FindMaterialDrawReflection(passMaterial);
				if (!reflection && !pass.shaderOverride) {
					reflection = context.renderPipeline->FindPipelineGraphicsReflection(pass.pipeline);
				}
				if (!reflection || seenReflections.count(reflection) != 0) {
					continue;
				}
				seenReflections.insert(reflection);
				reflections.push_back(reflection);
				if (const ShaderConstantBufferInfo* cb = FindConstantBuffer(*reflection, bufferName)) {
					for (const ShaderConstantBufferVariable& var : cb->variables) {
						if (MaterialParameterEditor::IsInternalPaddingParameter(var)) {
							continue;
						}
						reflectedNames.insert(var.name);
					}
				}
				// space2のTextureも編集対象へ加える
				for (const ShaderResourceBinding& res : reflection->resources) {
					if (MaterialParameterEditor::IsMaterialTextureResource(res)) {
						reflectedNames.insert(
							std::string(MaterialParameterEditor::GetReflectedTextureDisplayName(res, *reflection)));
					}
				}
			}
		}

		// Shaderの公開値を編集する
		ImGui::Spacing();
		if (MyGUI::CollapsingHeader("シェーダーパラメータ", true)) {

			std::unordered_set<uint64_t> drawnParameters;
			for (const ShaderReflectionInfo* reflection : reflections) {
				result.Merge(MaterialParameterEditor::DrawReflectedCBufferParameters(
					*reflection, bufferName, draft.parameters, drawnParameters));
			}
			// 未描画の材質も保存済みの値を編集できる
			std::vector<MaterialParameterID> stored;
			for (const auto& record : draft.parameters.GetRecords()) {
				if (!reflectedNames.contains(record.namedValue.first) &&
					MaterialParameterEditor::IsExposedParameter(record.id, parameterVisibility)) {
					stored.push_back(record.id);
				}
			}
			result.Merge(MaterialParameterEditor::DrawGraphOverrides(
				draft.parameters, draft.parameters, stored, context.editorContext->assetDatabase));
		}

		// Textureを公開IDごとに一度だけ表示する
		ImGui::Spacing();
		if (!reflections.empty() && MyGUI::CollapsingHeader("テクスチャ", true)) {

			std::unordered_set<uint64_t> drawnTextures;
			bool anyTexture = false;
			const auto drawTexture = [&](MaterialParameterID parameterID, MaterialParameterSemantic semantic,
										 std::string_view displayName) {
				if (!parameterID || drawnTextures.count(parameterID.value) != 0) {
					return;
				}
				drawnTextures.insert(parameterID.value);
				anyTexture = true;

				AssetID textureID{};
				const MaterialParameterValue* value = draft.parameters.Find(parameterID);
				if (!value && semantic != MaterialParameterSemantic::None) {
					value = draft.parameters.Find(semantic);
				}
				if (!value) {
					value = draft.parameters.FindByName(displayName);
				}
				if (value) {
					if (const AssetID* id = std::get_if<AssetID>(&value->value)) {
						textureID = *id;
					}
				}
				const auto edit = MyGUI::AssetReferenceField(displayName.data(), textureID, context.editorContext->assetDatabase,
					{AssetType::Texture, AssetType::RenderTexture});
				result.Merge(edit);
				if (edit.valueChanged) {

					MaterialParameterValue parameter{};
					parameter.value = textureID;
					draft.parameters.Set(parameterID, displayName, semantic, parameter);
				}
			};

			for (const ShaderReflectionInfo* reflection : reflections) {
				MaterialParameterLayout layout{};
				layout.Build(*reflection, bufferName);
				if (layout.IsValid()) {
					for (const ShaderConstantBufferVariable& variable : layout.GetVariables()) {

						if (variable.used && MaterialParameterEditor::IsExposedParameter(variable) &&
							MaterialParameterEditor::IsReflectedTextureParam(variable, *reflection)) {
							drawTexture(variable.parameterID, variable.semantic, variable.name);
						}
					}
				}
				for (const ShaderResourceBinding& res : reflection->resources) {

					if (!MaterialParameterEditor::IsExposedTextureResource(res, *reflection)) {
						continue;
					}
					const MaterialParameterID parameterID =
						MaterialParameterEditor::GetReflectedTextureParameterID(res, *reflection);
					const std::string_view displayName = MaterialParameterEditor::GetReflectedTextureDisplayName(res, *reflection);
					const MaterialParameterSemantic semantic =
						MaterialParameterEditor::GetReflectedTextureSemantic(res, *reflection);
					drawTexture(parameterID, semantic, displayName);
				}
			}
			if (!anyTexture) {
				ImGui::TextDisabled("テクスチャスロットはありません");
			}
		}
	}

	void DrawAdvancedFields(const EditorPanelContext& context, MaterialAsset& draft, ValueEditResult& result,
		const std::unordered_set<std::string>& reflectedNames) {

		ImGui::Spacing();
		if (MyGUI::CollapsingHeader("詳細設定", false)) {
			result.Merge(InspectorDrawerCommon::DrawEnumComboField("ドメイン", draft.domain));

			if (ImGui::Button("標準Mesh設定に戻す", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

				const AssetID guid = draft.guid;
				draft = CreateDefaultMeshMaterialAsset(draft.name);
				draft.guid = guid;
				result.valueChanged = result.editFinished = true;
			}

			int32_t removeIndex = -1;
			for (int32_t index = 0; index < static_cast<int32_t>(draft.passes.size()); ++index) {

				MaterialPassBinding& pass = draft.passes[index];
				ImGui::PushID(index);
				const std::string_view passLabel = EnumAdapter<MaterialPassKind>::ToStringView(pass.passKind);
				if (ImGui::TreeNodeEx(
						"Pass", ImGuiTreeNodeFlags_DefaultOpen, "%.*s", static_cast<int>(passLabel.size()), passLabel.data())) {

					ValueEditResult passKindResult = InspectorDrawerCommon::DrawEnumComboField("パスの種類", pass.passKind);
					result.Merge(passKindResult);

					result.Merge(DrawPassReference(context, "Pipeline", pass.pipeline, AssetType::RenderPipeline));
					result.Merge(DrawPassReference(context, "Shader Override", pass.shaderOverride, AssetType::Shader));

					ValueEditResult variantResult = InspectorDrawerCommon::DrawEnumComboField("Variant", pass.preferredVariant);
					result.Merge(variantResult);

					if (ImGui::Button("パスを削除", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

						removeIndex = index;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			if (0 <= removeIndex && removeIndex < static_cast<int32_t>(draft.passes.size())) {

				draft.passes.erase(draft.passes.begin() + removeIndex);
				result.valueChanged = result.editFinished = true;
			}
			if (ImGui::Button("パスを追加", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

				draft.passes.push_back({
					.passKind = MaterialPassKind::Draw,
					.pipeline = {},
					.preferredVariant = PipelineVariantKind::GraphicsMesh,
				});
				result.valueChanged = result.editFinished = true;
			}

			std::string removeKey;
			if (ImGui::BeginMenu("追加パラメータを削除")) {
				for (const auto& record : draft.parameters.GetRecords()) {
					const auto& name = record.namedValue.first;
					if (!reflectedNames.contains(name) && ImGui::MenuItem(name.c_str())) removeKey = name;
				}
				ImGui::EndMenu();
			}
			if (!removeKey.empty()) {

				draft.parameters.erase(removeKey);
				result.valueChanged = result.editFinished = true;
			}

			if (ImGui::Button("ベースカラーを追加", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

				draft.parameters.Set(MaterialParameterIDs::BaseColor, MaterialParameterNames::BaseColor,
					MaterialParameterSemantic::BaseColor, MaterialParameterValue{.value = Color4::White()});
				result.valueChanged = result.editFinished = true;
			}
			if (ImGui::Button("ベースカラーテクスチャを追加", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

				draft.parameters.Set(MaterialParameterIDs::BaseColorTexture, MaterialParameterNames::BaseColorTexture,
					MaterialParameterSemantic::BaseColorTexture, MaterialParameterValue{.value = AssetID{}});
				result.valueChanged = result.editFinished = true;
			}
			if (ImGui::Button("Floatを追加", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

				std::string name = "Float";
				uint32_t suffix = 1;
				while (draft.parameters.contains(name)) {
					name = "Float" + std::to_string(suffix++);
				}
				draft.parameters.Set(name, MaterialParameterValue{.value = 0.0f});
				result.valueChanged = result.editFinished = true;
			}
		}
	}
}
}

Engine::ValueEditResult Engine::MaterialAssetFields::Draw(const EditorPanelContext& context, MaterialAsset& draft) {

	ValueEditResult result{};
	std::unordered_set<std::string> reflectedNames;
	if (draft.domain == MaterialDomain::Decal) {
		result.Merge(DecalMaterialFields::Draw(draft.parameters, draft, context.editorContext->assetDatabase));
	} else {
		DrawShaderFields(context, draft, result, reflectedNames);
	}
	DrawAdvancedFields(context, draft, result, reflectedNames);
	return result;
}
