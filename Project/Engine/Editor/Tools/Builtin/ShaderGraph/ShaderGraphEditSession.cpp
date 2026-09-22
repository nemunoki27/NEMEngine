#include "ShaderGraphEditSession.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphPublication.h"
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <filesystem>
#include <span>
#include <utility>

using namespace Engine::ShaderGraphPublication;

namespace {

	std::string MakeShaderGraphCompileState(
		const Engine::ShaderGraphAsset& graph) {

		nlohmann::json data = Engine::ToJson(graph);
		data.erase("groups");
		if (data.contains("nodes") && data["nodes"].is_array()) {
			for (nlohmann::json& node : data["nodes"]) {
				node.erase("groupID");
				node.erase("position");
				node.erase("previewExpanded");
			}
		}
		return data.dump();
	}

	bool HasCurrentGraphDefaults(
		const Engine::MaterialAsset& material,
		const Engine::MaterialAsset& expected) {

		if (material.shaderGraph != expected.shaderGraph) {
			return false;
		}
		const std::span<const Engine::MaterialParameterRecord> records =
			material.parameters.GetRecords();
		const std::span<const Engine::MaterialParameterRecord> expectedRecords =
			expected.parameters.GetRecords();
		if (records.size() != expectedRecords.size()) {
			return false;
		}
		for (size_t index = 0; index < records.size(); ++index) {
			if (records[index].id != expectedRecords[index].id ||
				records[index].namedValue.first !=
				expectedRecords[index].namedValue.first ||
				records[index].semantic != expectedRecords[index].semantic ||
				Engine::SerializeMaterialParameterValue(
					records[index].namedValue.second) !=
				Engine::SerializeMaterialParameterValue(
					expectedRecords[index].namedValue.second)) {

				return false;
			}
		}
		return true;
	}
}

bool Engine::ShaderGraphEditSession::Load(const EditorToolContext& context, AssetID assetID) {

	AssetDatabase* database =
		context.toolContext.assetDatabase;
	if (!database || !assetID) {
		selectedAsset_ = {};
		previewMaterial_ = {};
		graphLoaded_ = false;
		graphDirty_ = false;
		previewCompileDirty_ = false;
		compiledGraphState_.clear();
		return false;
	}

	const std::filesystem::path path =
		database->ResolveFullPath(assetID);
	ShaderGraphAsset loaded{};
	if (path.empty() ||
		!FromJson(JsonAdapter::Load(path, false), loaded)) {

		statusMessage_ =
			"グラフを読み込めませんでした";
		return false;
	}

	selectedAsset_ = assetID;
	graph_ = std::move(loaded);
	history_.Reset(graph_);
	const std::filesystem::path materialPath =
		path.parent_path() /
		Algorithm::PathFromUTF8(
			GraphFileStem(path) + ".material.json");
	previewMaterial_ = std::filesystem::exists(materialPath) ?
		database->ImportOrGet(
			RuntimePaths::ToAssetPath(materialPath),
			AssetType::Material) : AssetID{};
	compiledGraphState_ = MakeShaderGraphCompileState(graph_);
	previewCompileDirty_ = !previewMaterial_;
	if (previewMaterial_) {
		MaterialAsset material{};
		const MaterialAsset expected =
			ShaderGraphArtifactCache::CreateMaterial(
				graph_, selectedAsset_);
		previewCompileDirty_ =
			!FromJson(
				JsonAdapter::Load(materialPath, false), material) ||
			!HasCurrentGraphDefaults(material, expected);
	}
	graphLoaded_ = true;
	graphDirty_ = false;
	latestDiagnostics_.clear();
	statusMessage_.clear();
	return true;
}

std::filesystem::path Engine::ShaderGraphEditSession::ResolveCompilePath(const EditorToolContext& context) {

	AssetDatabase* database =
		context.toolContext.assetDatabase;
	if (!database || !selectedAsset_) {
		return {};
	}
	const std::filesystem::path graphPath =
		database->ResolveFullPath(selectedAsset_);
	if (graphPath.empty()) {
		statusMessage_ = "グラフのパスを解決できません";
		return {};
	}

	return graphPath;
}

bool Engine::ShaderGraphEditSession::SaveAndCompile(const EditorToolContext& context,
	const std::filesystem::path& graphPath) {

	AssetDatabase* database = context.toolContext.assetDatabase;
	if (!ShaderGraphPublication::CompileAndPublish(context, *database, graph_, selectedAsset_, graphPath,
		previewMaterial_, latestDiagnostics_, statusMessage_)) {
		return false;
	}
	graphDirty_ = false;
	compiledGraphState_ = MakeShaderGraphCompileState(graph_);
	previewCompileDirty_ = false;
	statusMessage_ = "コンパイルしました";
	return true;
}

void Engine::ShaderGraphEditSession::Save(const EditorToolContext& context) {

	AssetDatabase* database = context.toolContext.assetDatabase;
	if (const AssetMeta* meta = database ? database->Find(selectedAsset_) : nullptr) {
		JsonAdapter::Save(database->ResolveFullPath(meta->guid), ToJson(graph_));
		graphDirty_ = false;
		statusMessage_ = "保存しました";
	}
}

void Engine::ShaderGraphEditSession::Import(ShaderGraphAsset imported) {

	graph_ = std::move(imported);
	history_.Commit(graph_);
	graphDirty_ = true;
	previewCompileDirty_ = true;
	compiledGraphState_.clear();
	latestDiagnostics_.clear();
}

void Engine::ShaderGraphEditSession::CaptureHistory() {

	history_.Commit(graph_);
}

void Engine::ShaderGraphEditSession::Commit() {

	if (history_.Commit(graph_)) {
		UpdateCompileState();
	}
}

bool Engine::ShaderGraphEditSession::Undo() {

	if (!history_.Undo(graph_)) {
		return false;
	}
	UpdateCompileState();
	return true;
}

bool Engine::ShaderGraphEditSession::Redo() {

	if (!history_.Redo(graph_)) {
		return false;
	}
	UpdateCompileState();
	return true;
}

void Engine::ShaderGraphEditSession::UpdateCompileState() {

	graphDirty_ = true;
	previewCompileDirty_ = MakeShaderGraphCompileState(graph_) != compiledGraphState_;
}
