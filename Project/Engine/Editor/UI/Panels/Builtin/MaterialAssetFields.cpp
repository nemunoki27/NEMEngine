#include "MaterialAssetFields.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Editor/UI/Common/MaterialParameterEditor.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <limits>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace {

	// 保存値の型名を表示する
	const char* GetMaterialParameterTypeName(const Engine::MaterialParameterValue& parameter) {

		return std::visit(
			[](const auto& value) -> const char* {
				using ValueType = std::decay_t<decltype(value)>;

				if constexpr (std::is_same_v<ValueType, float>) {
					return "Float";
				} else if constexpr (std::is_same_v<ValueType, Engine::Vector2>) {
					return "Vector2";
				} else if constexpr (std::is_same_v<ValueType, Engine::Vector3>) {
					return "Vector3";
				} else if constexpr (std::is_same_v<ValueType, Engine::Vector4>) {
					return "Vector4";
				} else if constexpr (std::is_same_v<ValueType, Engine::Color4>) {
					return "Color4";
				} else if constexpr (std::is_same_v<ValueType, Engine::AssetID>) {
					return "Asset";
				} else if constexpr (std::is_same_v<ValueType, int32_t>) {
					return "Int";
				} else if constexpr (std::is_same_v<ValueType, uint32_t>) {
					return "UInt";
				} else if constexpr (std::is_same_v<ValueType, bool>) {
					return "Bool";
				} else {
					return "Unknown";
				}
			},
			parameter.value);
	}

	// 保存値の型に合わせて編集する
	Engine::ValueEditResult DrawMaterialParameterValue(
		const char* label, Engine::MaterialParameterValue& parameter, const Engine::AssetDatabase* assetDatabase) {

		return std::visit(
			[&](auto& value) -> Engine::ValueEditResult {
				using ValueType = std::decay_t<decltype(value)>;

				if constexpr (std::is_same_v<ValueType, float>) {
					return Engine::MyGUI::DragFloat(label, value);
				} else if constexpr (std::is_same_v<ValueType, Engine::Vector2>) {
					return Engine::MyGUI::DragVector2(label, value);
				} else if constexpr (std::is_same_v<ValueType, Engine::Vector3>) {
					return Engine::MyGUI::DragVector3(label, value);
				} else if constexpr (std::is_same_v<ValueType, Engine::Vector4>) {
					return Engine::MyGUI::DragVector4(label, value);
				} else if constexpr (std::is_same_v<ValueType, Engine::Color4>) {
					return Engine::MyGUI::ColorEdit(label, value);
				} else if constexpr (std::is_same_v<ValueType, Engine::AssetID>) {
					return Engine::MyGUI::AssetReferenceField(
						label, value, assetDatabase, {Engine::AssetType::Texture, Engine::AssetType::RenderTexture});
				} else if constexpr (std::is_same_v<ValueType, int32_t>) {
					return Engine::MyGUI::DragInt(label, value);
				} else if constexpr (std::is_same_v<ValueType, uint32_t>) {
					int32_t signedValue = static_cast<int32_t>(value);
					Engine::ValueEditResult result = Engine::MyGUI::DragInt(label, signedValue,
						{.dragSpeed = 1.0f, .minValue = 0, .maxValue = (std::numeric_limits<int32_t>::max)()});
					if (result.valueChanged) {
						value = static_cast<uint32_t>(signedValue);
					}
					return result;
				} else if constexpr (std::is_same_v<ValueType, bool>) {
					return Engine::InspectorDrawerCommon::DrawCheckboxField(label, value);
				} else {
					return Engine::ValueEditResult{};
				}
			},
			parameter.value);
	}
}

//============================================================================
//	MaterialAssetFields functions
//============================================================================

bool Engine::MaterialAssetFields::Draw(const EditorPanelContext& context, MaterialAsset& draft) {

	bool saveRequested = false;

	ImGui::Text("Material");
	ImGui::Separator();

	ValueEditResult nameResult = MyGUI::InputText("Name", draft.name);
	saveRequested |= nameResult.editFinished;

	ValueEditResult domainResult = InspectorDrawerCommon::DrawEnumComboField("Domain", draft.domain);
	saveRequested |= domainResult.editFinished;

	if (ImGui::Button("Use Mesh Template", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

		const AssetID guid = draft.guid;
		draft = CreateDefaultMeshMaterialAsset(draft.name);
		draft.guid = guid;
		saveRequested = true;
	}

	ImGui::Spacing();
	if (MyGUI::CollapsingHeader("Passes")) {

		int32_t removeIndex = -1;
		for (int32_t index = 0; index < static_cast<int32_t>(draft.passes.size()); ++index) {

			MaterialPassBinding& pass = draft.passes[index];
			ImGui::PushID(index);
			const std::string_view passLabel = EnumAdapter<MaterialPassKind>::ToStringView(pass.passKind);
			if (ImGui::TreeNodeEx(
					"Pass", ImGuiTreeNodeFlags_DefaultOpen, "%.*s", static_cast<int>(passLabel.size()), passLabel.data())) {

				ValueEditResult passKindResult = InspectorDrawerCommon::DrawEnumComboField("Pass Kind", pass.passKind);
				saveRequested |= passKindResult.editFinished;

				ValueEditResult pipelineResult = MyGUI::AssetReferenceField(
					"Pipeline", pass.pipeline, context.editorContext->assetDatabase, {AssetType::RenderPipeline});
				saveRequested |= pipelineResult.editFinished;

				ValueEditResult shaderResult = MyGUI::AssetReferenceField(
					"Shader Override", pass.shaderOverride, context.editorContext->assetDatabase, {AssetType::Shader});
				saveRequested |= shaderResult.editFinished;

				ValueEditResult variantResult = InspectorDrawerCommon::DrawEnumComboField("Variant", pass.preferredVariant);
				saveRequested |= variantResult.editFinished;

				if (ImGui::Button("Remove Pass", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

					removeIndex = index;
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		if (0 <= removeIndex && removeIndex < static_cast<int32_t>(draft.passes.size())) {

			draft.passes.erase(draft.passes.begin() + removeIndex);
			saveRequested = true;
		}
		if (ImGui::Button("Add Pass", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

			draft.passes.push_back({
				.passKind = MaterialPassKind::Draw,
				.pipeline = {},
				.preferredVariant = PipelineVariantKind::GraphicsMesh,
			});
			saveRequested = true;
		}
	}

	// 描画済みPipelineから公開パラメータを集める
	std::unordered_set<std::string> reflectedNames;
	std::vector<const ShaderReflectionInfo*> reflections;
	if (context.renderPipeline) {

		std::unordered_set<const ShaderReflectionInfo*> seenReflections;
		for (const MaterialPassBinding& pass : draft.passes) {

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
			if (const ShaderConstantBufferInfo* cb = FindConstantBuffer(*reflection, MaterialParameterCBuffer::kSurface)) {
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
	if (MyGUI::CollapsingHeader("Shader Parameters", true)) {

		if (reflections.empty()) {

			ImGui::TextDisabled("シェーダー未構築か MaterialParameters cbuffer がありません");
			ImGui::TextDisabled("対象マテリアルが一度描画されると自動で列挙されます");
		} else {
			for (const ShaderReflectionInfo* reflection : reflections) {
				if (MaterialParameterEditor::DrawReflectedCBufferParameters(
						*reflection, MaterialParameterCBuffer::kSurface, draft.parameters)) {

					saveRequested = true;
				}
			}
		}
	}

	// Textureを公開IDごとに一度だけ表示する
	ImGui::Spacing();
	if (MyGUI::CollapsingHeader("Shader Textures", true)) {

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
			if (MyGUI::AssetReferenceField(displayName.data(), textureID, context.editorContext->assetDatabase,
					{AssetType::Texture, AssetType::RenderTexture})
					.editFinished) {

				MaterialParameterValue parameter{};
				parameter.value = textureID;
				draft.parameters.Set(parameterID, displayName, semantic, parameter);
				saveRequested = true;
			}
		};

		for (const ShaderReflectionInfo* reflection : reflections) {
			MaterialParameterLayout layout{};
			layout.Build(*reflection, MaterialParameterCBuffer::kSurface);
			if (layout.IsValid()) {
				for (const ShaderConstantBufferVariable& variable : layout.GetVariables()) {

					if (variable.used && MaterialParameterEditor::IsReflectedTextureParam(variable, *reflection)) {
						drawTexture(variable.parameterID, variable.semantic, variable.name);
					}
				}
			}
			for (const ShaderResourceBinding& res : reflection->resources) {

				if (!MaterialParameterEditor::IsMaterialTextureResource(res)) {
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
			ImGui::TextDisabled("space2のマテリアルテクスチャがありません");
		}
	}

	ImGui::Spacing();
	if (MyGUI::CollapsingHeader("Custom Parameters")) {

		std::string removeKey;
		for (const MaterialParameterRecord& record : draft.parameters.GetRecords()) {

			const std::string& key = record.namedValue.first;
			MaterialParameterValue parameter = record.namedValue.second;

			// Shader側で表示した値を除外する
			if (reflectedNames.count(key) != 0) {
				continue;
			}

			ImGui::PushID(key.c_str());
			ImGui::TextDisabled("%s", GetMaterialParameterTypeName(parameter));
			ValueEditResult parameterResult =
				DrawMaterialParameterValue(key.c_str(), parameter, context.editorContext->assetDatabase);
			if (parameterResult.valueChanged) {
				draft.parameters.Set(record.id, key, record.semantic, parameter);
			}
			saveRequested |= parameterResult.editFinished;
			if (ImGui::Button("Remove", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

				removeKey = key;
			}
			ImGui::Separator();
			ImGui::PopID();
		}
		if (!removeKey.empty()) {

			draft.parameters.erase(removeKey);
			saveRequested = true;
		}

		if (ImGui::Button("Add BaseColor", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

			draft.parameters.Set(MaterialParameterIDs::BaseColor, MaterialParameterNames::BaseColor,
				MaterialParameterSemantic::BaseColor, MaterialParameterValue{.value = Color4::White()});
			saveRequested = true;
		}
		if (ImGui::Button("Add MainTexture", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

			draft.parameters.Set(MaterialParameterIDs::BaseColorTexture, MaterialParameterNames::BaseColorTexture,
				MaterialParameterSemantic::BaseColorTexture, MaterialParameterValue{.value = AssetID{}});
			saveRequested = true;
		}
		if (ImGui::Button("Add Float", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

			std::string name = "Float";
			uint32_t suffix = 1;
			while (draft.parameters.contains(name)) {
				name = "Float" + std::to_string(suffix++);
			}
			draft.parameters.Set(name, MaterialParameterValue{.value = 0.0f});
			saveRequested = true;
		}
	}

	return saveRequested;
}
