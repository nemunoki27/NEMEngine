#include "InspectorMaterialSession.h"
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Tools/ImGui/ImGuiEnum.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Editor/UI/Common/MaterialParameterEditor.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Editor/Utility/EditorTextureHelper.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

// c++
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <vector>
#include <span>


namespace {

	// Material JSON内のパス参照をAssetIDに解決して、Inspectorで編集できる形にする
	void ResolveMaterialPipelineReferences(Engine::AssetDatabase& database, nlohmann::json& data) {

		if (!data.contains("passes") || !data["passes"].is_array()) {
			return;
		}
		for (auto& passJson : data["passes"]) {

			if (!passJson.is_object() || !passJson["pipeline"].is_string()) {
				continue;
			}

			const std::string text = passJson["pipeline"].get<std::string>();
			if (text.empty() || Engine::TryParseAssetGUID32Hex(text)) {
				continue;
			}

			const std::filesystem::path fullPath = database.ResolveAssetPath(text);
			if (!std::filesystem::exists(fullPath)) {
				continue;
			}

			const Engine::AssetID pipeline = database.ImportOrGet(text, Engine::AssetType::RenderPipeline);
			if (pipeline) {
				passJson["pipeline"] = Engine::ToString(pipeline);
			}
		}
	}

	// MaterialDomainの編集フィールドを描画する
	Engine::ValueEditResult DrawMaterialDomainField(const char* label, Engine::MaterialDomain& value) {

		Engine::ValueEditResult result{};
		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return result;
		}

		Engine::MaterialDomain edited = value;
		result.valueChanged = Engine::ImGuiUtility::EnumCombo<Engine::MaterialDomain>("##Value", &edited);
		if (result.valueChanged) {
			value = edited;
		}
		result.anyItemActive = ImGui::IsItemActive();
		result.editFinished = result.valueChanged || ImGui::IsItemDeactivatedAfterEdit();
		Engine::MyGUI::EndPropertyRow();
		return result;
	}

	// PipelineVariantKindの編集フィールドを描画する
	Engine::ValueEditResult DrawPipelineVariantField(const char* label, Engine::PipelineVariantKind& value) {

		Engine::ValueEditResult result{};
		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return result;
		}

		Engine::PipelineVariantKind edited = value;
		result.valueChanged = Engine::ImGuiUtility::EnumCombo<Engine::PipelineVariantKind>("##Value", &edited);
		if (result.valueChanged) {
			value = edited;
		}
		result.anyItemActive = ImGui::IsItemActive();
		result.editFinished = result.valueChanged || ImGui::IsItemDeactivatedAfterEdit();
		Engine::MyGUI::EndPropertyRow();
		return result;
	}

	// MaterialPassKindの編集フィールドを描画する
	Engine::ValueEditResult DrawMaterialPassKindField(const char* label, Engine::MaterialPassKind& value) {

		Engine::ValueEditResult result{};
		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return result;
		}

		Engine::MaterialPassKind edited = value;
		result.valueChanged = Engine::ImGuiUtility::EnumCombo<Engine::MaterialPassKind>("##Value", &edited);
		if (result.valueChanged) {
			value = edited;
		}
		result.anyItemActive = ImGui::IsItemActive();
		result.editFinished = result.valueChanged || ImGui::IsItemDeactivatedAfterEdit();
		Engine::MyGUI::EndPropertyRow();
		return result;
	}

	// Materialパラメーターの型名を表示用に取得する
	const char* GetMaterialParameterTypeName(const Engine::MaterialParameterValue& parameter) {

		return std::visit([](const auto& value) -> const char* {
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
			} else {
				return "Unknown";
			}
			}, parameter.value);
	}

	// Materialパラメーターの値を型に応じたUIで描画する
	Engine::ValueEditResult DrawMaterialParameterValue(const char* label,
		Engine::MaterialParameterValue& parameter, const Engine::AssetDatabase* assetDatabase) {

		return std::visit([&](auto& value) -> Engine::ValueEditResult {
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
				return Engine::MyGUI::AssetReferenceField(label, value, assetDatabase, { Engine::AssetType::Texture });
			} else if constexpr (std::is_same_v<ValueType, int32_t>) {
				return Engine::MyGUI::DragInt(label, value);
			} else if constexpr (std::is_same_v<ValueType, uint32_t>) {
				int32_t signedValue = static_cast<int32_t>(value);
				Engine::ValueEditResult result = Engine::MyGUI::DragInt(label, signedValue,
					{ .dragSpeed = 1.0f, .minValue = 0, .maxValue = (std::numeric_limits<int32_t>::max)() });
				if (result.valueChanged) {
					value = static_cast<uint32_t>(signedValue);
				}
				return result;
			} else {
				return Engine::ValueEditResult{};
			}
			}, parameter.value);
	}

}

void Engine::InspectorMaterialSession::DrawMaterialAssetInspector(const EditorPanelContext& context, const AssetMeta& meta) {

	if (!LoadMaterialDraft(context, meta)) {

		ImGui::TextDisabled("Failed to load material.");
		return;
	}

	bool saveRequested = false;

	ImGui::Text("Material");
	ImGui::Separator();

	ValueEditResult nameResult = MyGUI::InputText("Name", materialDraft_.name);
	saveRequested |= nameResult.editFinished;

	ValueEditResult domainResult = DrawMaterialDomainField("Domain", materialDraft_.domain);
	saveRequested |= domainResult.editFinished;

	if (ImGui::Button("Use Mesh Template", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

		const AssetID guid = materialDraft_.guid;
		materialDraft_ = CreateDefaultMeshMaterialAsset(materialDraft_.name);
		materialDraft_.guid = guid;
		saveRequested = true;
	}

	ImGui::Spacing();
	if (MyGUI::CollapsingHeader("Passes")) {

		int32_t removeIndex = -1;
		for (int32_t index = 0; index < static_cast<int32_t>(materialDraft_.passes.size()); ++index) {

			MaterialPassBinding& pass = materialDraft_.passes[index];
			ImGui::PushID(index);
			const std::string_view passLabel = EnumAdapter<MaterialPassKind>::ToStringView(pass.passKind);
			if (ImGui::TreeNodeEx("Pass", ImGuiTreeNodeFlags_DefaultOpen, "%.*s",
				static_cast<int>(passLabel.size()), passLabel.data())) {

				ValueEditResult passKindResult = DrawMaterialPassKindField("Pass Kind", pass.passKind);
				saveRequested |= passKindResult.editFinished;

				ValueEditResult pipelineResult = MyGUI::AssetReferenceField("Pipeline", pass.pipeline,
					context.editorContext->assetDatabase, { AssetType::RenderPipeline });
				saveRequested |= pipelineResult.editFinished;

				ValueEditResult shaderResult = MyGUI::AssetReferenceField("Shader Override", pass.shaderOverride,
					context.editorContext->assetDatabase, { AssetType::Shader });
				saveRequested |= shaderResult.editFinished;

				ValueEditResult variantResult = DrawPipelineVariantField("Variant", pass.preferredVariant);
				saveRequested |= variantResult.editFinished;

				if (ImGui::Button("Remove Pass", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

					removeIndex = index;
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		if (0 <= removeIndex && removeIndex < static_cast<int32_t>(materialDraft_.passes.size())) {

			materialDraft_.passes.erase(materialDraft_.passes.begin() + removeIndex);
			saveRequested = true;
		}
		if (ImGui::Button("Add Pass", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

			materialDraft_.passes.push_back({
				.passKind = MaterialPassKind::Draw,
				.pipeline = {},
				.preferredVariant = PipelineVariantKind::GraphicsMesh,
				});
			saveRequested = true;
		}
	}

	// シェーダーが要求するパラメータ名をリフレクションから集める、描画済みパイプラインのみ取得できる
	std::unordered_set<std::string> reflectedNames;
	std::vector<const ShaderReflectionInfo*> reflections;
	if (context.renderPipeline) {

		std::unordered_set<const ShaderReflectionInfo*> seenReflections;
		for (const MaterialPassBinding& pass : materialDraft_.passes) {

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
			// space2のテクスチャSRVもマテリアルテクスチャとして自動列挙対象にする
			for (const ShaderResourceBinding& res : reflection->resources) {
				if (MaterialParameterEditor::IsMaterialTextureResource(res)) {
					reflectedNames.insert(std::string(
						MaterialParameterEditor::GetReflectedTextureDisplayName(
							res, *reflection)));
				}
			}
		}
	}

	// シェーダーのMaterialParameters cbufferを最初から編集可能な状態で自動列挙する
	ImGui::Spacing();
	if (MyGUI::CollapsingHeader("Shader Parameters", true)) {

		if (reflections.empty()) {

			ImGui::TextDisabled("シェーダー未構築か MaterialParameters cbuffer がありません");
			ImGui::TextDisabled("対象マテリアルが一度描画されると自動で列挙されます");
		} else {
			for (const ShaderReflectionInfo* reflection : reflections) {
				if (MaterialParameterEditor::DrawReflectedCBufferParameters(
					*reflection, MaterialParameterCBuffer::kSurface, materialDraft_.parameters)) {

					saveRequested = true;
				}
			}
		}
	}

	// space2のマテリアルテクスチャをリフレクションから自動列挙する、未指定なら描画時に白テクスチャになる
	ImGui::Spacing();
	if (MyGUI::CollapsingHeader("Shader Textures", true)) {

		std::unordered_set<uint64_t> drawnTextures;
		bool anyTexture = false;
		const auto drawTexture = [&](MaterialParameterID parameterID,
			MaterialParameterSemantic semantic, std::string_view displayName) {

			if (!parameterID ||
				drawnTextures.count(parameterID.value) != 0) {
				return;
			}
			drawnTextures.insert(parameterID.value);
			anyTexture = true;

			AssetID textureID{};
			const MaterialParameterValue* value =
				materialDraft_.parameters.Find(parameterID);
			if (!value && semantic != MaterialParameterSemantic::None) {
				value = materialDraft_.parameters.Find(semantic);
			}
			if (!value) {
				value = materialDraft_.parameters.FindByName(displayName);
			}
			if (value) {
				if (const AssetID* id =
					std::get_if<AssetID>(&value->value)) {
					textureID = *id;
				}
			}
			if (MyGUI::AssetReferenceField(
				displayName.data(), textureID,
				context.editorContext->assetDatabase,
				{ AssetType::Texture }).editFinished) {

				MaterialParameterValue parameter{};
				parameter.value = textureID;
				materialDraft_.parameters.Set(
					parameterID, displayName, semantic, parameter);
				saveRequested = true;
			}
			};

		for (const ShaderReflectionInfo* reflection : reflections) {
			MaterialParameterLayout layout{};
			layout.Build(
				*reflection, MaterialParameterCBuffer::kSurface);
			if (layout.IsValid()) {
				for (const ShaderConstantBufferVariable& variable :
					layout.GetVariables()) {

					if (variable.used &&
						MaterialParameterEditor::IsReflectedTextureParam(
							variable, *reflection)) {
						drawTexture(variable.parameterID,
							variable.semantic, variable.name);
					}
				}
			}
			for (const ShaderResourceBinding& res : reflection->resources) {

				if (!MaterialParameterEditor::IsMaterialTextureResource(res)) {
					continue;
				}
				const MaterialParameterID parameterID =
					MaterialParameterEditor::GetReflectedTextureParameterID(
						res, *reflection);
				const std::string_view displayName =
					MaterialParameterEditor::GetReflectedTextureDisplayName(
						res, *reflection);
				const MaterialParameterSemantic semantic =
					MaterialParameterEditor::GetReflectedTextureSemantic(
						res, *reflection);
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
		for (const MaterialParameterRecord& record : materialDraft_.parameters.GetRecords()) {

			const std::string& key = record.namedValue.first;
			MaterialParameterValue parameter = record.namedValue.second;

			// シェーダーが要求するパラメータはShader Parametersで編集するため重複表示しない
			if (reflectedNames.count(key) != 0) {
				continue;
			}

			ImGui::PushID(key.c_str());
			ImGui::TextDisabled("%s", GetMaterialParameterTypeName(parameter));
			ValueEditResult parameterResult = DrawMaterialParameterValue(key.c_str(), parameter,
				context.editorContext->assetDatabase);
			if (parameterResult.valueChanged) {
				materialDraft_.parameters.Set(record.id, key, record.semantic, parameter);
			}
			saveRequested |= parameterResult.editFinished;
			if (ImGui::Button("Remove", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

				removeKey = key;
			}
			ImGui::Separator();
			ImGui::PopID();
		}
		if (!removeKey.empty()) {

			materialDraft_.parameters.erase(removeKey);
			saveRequested = true;
		}

		if (ImGui::Button("Add BaseColor", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

			materialDraft_.parameters.Set(MaterialParameterIDs::BaseColor,
				MaterialParameterNames::BaseColor,
				MaterialParameterSemantic::BaseColor,
				MaterialParameterValue{ .value = Color4::White() });
			saveRequested = true;
		}
		if (ImGui::Button("Add MainTexture", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

			materialDraft_.parameters.Set(MaterialParameterIDs::BaseColorTexture,
				MaterialParameterNames::BaseColorTexture,
				MaterialParameterSemantic::BaseColorTexture,
				MaterialParameterValue{ .value = AssetID{} });
			saveRequested = true;
		}
		if (ImGui::Button("Add Float", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

			std::string name = "Float";
			uint32_t suffix = 1;
			while (materialDraft_.parameters.contains(name)) {
				name = "Float" + std::to_string(suffix++);
			}
			materialDraft_.parameters.Set(name, MaterialParameterValue{ .value = 0.0f });
			saveRequested = true;
		}
	}

	if (saveRequested) {

		SaveMaterialDraft(context, meta);
	}
}

bool Engine::InspectorMaterialSession::LoadMaterialDraft(const EditorPanelContext& context, const AssetMeta& meta) {

	if (materialDraftValid_ && editingMaterialAsset_ == meta.guid) {
		return true;
	}

	materialDraftValid_ = false;
	editingMaterialAsset_ = meta.guid;
	materialDraft_ = MaterialAsset{};

	if (!context.editorContext || !context.editorContext->assetDatabase) {
		return false;
	}

	const std::filesystem::path path = context.editorContext->assetDatabase->ResolveFullPath(meta.guid);
	if (path.empty()) {
		return false;
	}

	nlohmann::json data = JsonAdapter::Load(path.string(), false);
	ResolveMaterialPipelineReferences(*context.editorContext->assetDatabase, data);
	if (!FromJson(data, materialDraft_)) {
		return false;
	}
	if (!materialDraft_.guid) {
		materialDraft_.guid = meta.guid;
	}
	if (materialDraft_.name.empty()) {
		materialDraft_.name = path.stem().stem().string();
	}

	materialDraftValid_ = true;
	return true;
}

void Engine::InspectorMaterialSession::SaveMaterialDraft(const EditorPanelContext& context, const AssetMeta& meta) {

	if (!materialDraftValid_ || !context.editorContext || !context.editorContext->assetDatabase) {
		return;
	}

	materialDraft_.guid = meta.guid;

	const std::filesystem::path path = context.editorContext->assetDatabase->ResolveFullPath(meta.guid);
	if (path.empty()) {
		return;
	}

	JsonAdapter::Save(path.string(), ToJson(materialDraft_));

	// 実行中のマテリアルキャッシュを破棄して編集を即反映する、エディタを止めずに調整できるようにする
	if (context.renderPipeline) {
		context.renderPipeline->ReloadMaterial(meta.guid);
	}
}
