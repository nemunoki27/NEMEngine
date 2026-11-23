#include "GraphMaterialAssetFields.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Common/MaterialParameterEditor.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Editor/Tools/Builtin/ShaderGraph/ShaderGraphEditorTool.h>
#include <Engine/Core/Tools/Registry/ToolRegistry.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAssetReferences.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphParameterDefaults.h>

// c++
#include <algorithm>

//============================================================================
//	GraphMaterialAssetFields classMethods
//============================================================================
bool Engine::GraphMaterialAssetFields::Refresh(const AssetDatabase& database, AssetID graphID) {

	const auto lifetime = database.GetCacheLifetime().lock();
	const auto revision = database.GetContentRevision(graphID);
	if (graphID_ != graphID || databaseLifetime_.lock() != lifetime ||
		structureRevision_ != database.GetStructureRevision() || graphRevision_ != revision) {
		graphID_ = graphID;
		databaseLifetime_ = lifetime;
		structureRevision_ = database.GetStructureRevision();
		graphRevision_ = revision;
		ShaderGraphAsset graph;
		const auto* meta = database.Find(graphID_);
		loaded_ = meta && meta->type == AssetType::ShaderGraph && ShaderGraphAssetReferences::LoadSource(graphID_, &database, graph);
		diagnostic_.clear();
		if (loaded_) {
			graphName_ = graph.name.empty() ? meta->assetPath : graph.name;
			defaults_ = ShaderGraphParameterDefaults::Build(graph);
			exposed_ = ShaderGraphParameterDefaults::CollectExposed(graph);
		}
		return true;
	}
	return false;
}

Engine::ValueEditResult Engine::GraphMaterialAssetFields::Draw(
	const EditorPanelContext& context, MaterialAsset& material, bool readOnly) {

	auto& database = *context.editorContext->assetDatabase;

	ValueEditResult result{};
	if (!loaded_) {
		ImGui::TextWrapped("%s の参照元シェーダーグラフを読み込めません", material.name.c_str());
		AssetID replacement{};
		ImGui::BeginDisabled(readOnly);
		const auto edit = MyGUI::AssetReferenceField("参照元を再設定", replacement, &database, { AssetType::ShaderGraph });
		ImGui::EndDisabled();
		if (edit.valueChanged && replacement) {
			ShaderGraphAsset graph;
			ShaderGraphArtifact artifact;
			std::vector<ShaderGraphDiagnostic> diagnostics;
			if (ShaderGraphAssetReferences::LoadSource(replacement, &database, graph) &&
				ShaderGraphArtifactCache::Compile(graph, replacement, artifact, &database, &diagnostics)) {
				auto repaired = ShaderGraphArtifactCache::CreateMaterial(graph, replacement);
				ShaderGraphArtifactCache::ApplyToMaterial(artifact, repaired);
				ShaderGraphParameterDefaults::RestoreOverrides(graph, material, repaired);
				repaired.name = material.name;
				repaired.guid = material.guid;
				material = std::move(repaired);
				diagnostic_.clear();
				return edit;
			}
			diagnostic_ = diagnostics.empty() ? "指定されたグラフからマテリアルを生成できません" : diagnostics.front().message;
		}
		if (!diagnostic_.empty()) ImGui::TextWrapped("%s", diagnostic_.c_str());
		return result;
	}
	if (MyGUI::BeginPropertyRow("シェーダーグラフ")) {
		const std::string label = graphName_ + " を開く###OpenGraph";
		if (ImGui::Button(label.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
			if (auto* tool = dynamic_cast<ShaderGraphEditorTool*>(ToolRegistry::GetInstance().Find("engine.shader_graph"))) {
				tool->OpenAsset(graphID_);
			}
		}
		MyGUI::EndPropertyRow();
	}
	if (!MyGUI::CollapsingHeader("シェーダーパラメータ", true)) return result;
	if (exposed_.empty()) ImGui::TextDisabled("公開パラメータはありません");
	ImGui::BeginDisabled(readOnly);
	for (const auto id : exposed_) {
		const auto edit = MaterialParameterEditor::DrawGraphOverrides(material.parameters, defaults_,
			std::span(&id, 1), &database);
		if (edit.valueChanged) {
			const auto* value = material.parameters.Find(id);
			const auto* fallback = defaults_.Find(id);
			if (value && fallback && SerializeMaterialParameterValue(*value) == SerializeMaterialParameterValue(*fallback)) {
				std::erase(material.graphOverrides, id);
			} else if (std::ranges::find(material.graphOverrides, id) == material.graphOverrides.end()) {
				material.graphOverrides.push_back(id);
			}
		}
		result.Merge(edit);
	}
	ImGui::EndDisabled();
	return result;
}
