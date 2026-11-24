#include "ShaderGraphEditSession.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphPublication.h"
#include "ShaderGraphEditOperations.h"
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAssetReferences.h>

// c++
#include <algorithm>
#include <filesystem>
#include <utility>

using namespace Engine::ShaderGraphPublication;

namespace {

	std::string MakeShaderGraphCompileState(const Engine::ShaderGraphAsset& graph) {

		nlohmann::json data = Engine::ToJson(graph);
		data.erase("groups");
		if (data.contains("nodes") && data["nodes"].is_array()) {
			for (nlohmann::json& node : data["nodes"]) {
				node.erase("groupID");
				node.erase("position");
				node.erase("previewExpanded");
				node.erase("previewMode");
				node.erase("previewOutputSlot");
			}
		}
		return data.dump();
	}

}

void Engine::ShaderGraphEditSession::RemoveNode(UUID nodeID) {

	// Nodeの削除とdirty通知を同じ操作にする
	ShaderGraphEditOperations::RemoveNode(graph_, nodeID);
	MarkDirty();
}

bool Engine::ShaderGraphEditSession::ChangeTarget(ShaderGraphTarget target) {

	if (graph_.target == target) {
		return false;
	}
	graph_.target = target;
	// 描画対象に応じた出力Nodeと入力数へ揃える
	const bool is3D = IsShaderGraph3DTarget(target);
	for (ShaderGraphNode& node : graph_.nodes) {
		if (node.id == graph_.outputNode) {
			node.kind = target == ShaderGraphTarget::Decal ? ShaderGraphNodeKind::DecalOutput : is3D ? ShaderGraphNodeKind::SurfaceOutput : ShaderGraphNodeKind::UnlitOutput;
			break;
		}
	}
	const uint32_t inputCount = GetShaderGraphInputCount(target == ShaderGraphTarget::Decal ? ShaderGraphNodeKind::DecalOutput :
		is3D ? ShaderGraphNodeKind::SurfaceOutput : ShaderGraphNodeKind::UnlitOutput);
	std::erase_if(graph_.links,
		[&](const ShaderGraphLink& link) { return link.inputNode == graph_.outputNode && inputCount <= link.inputSlot; });
	// 非対応の頂点出力と接続を取り除く
	if (!SupportsShaderGraphVertexOutput(target) && graph_.vertexOutputNode) {
		ShaderGraphEditOperations::RemoveNode(graph_, graph_.vertexOutputNode);
	}
	MarkDirty();
	return true;
}

bool Engine::ShaderGraphEditSession::Load(const EditorToolContext& context, AssetID assetID) {

	AssetDatabase* database = context.toolContext.assetDatabase;
	if (!database || !assetID) {
		selectedAsset_ = {};
		previewMaterial_ = {};
		graphLoaded_ = false;
		graphDirty_ = false;
		previewCompileDirty_ = false;
		compiledGraphState_.clear();
		return false;
	}

	const std::filesystem::path path = database->ResolveFullPath(assetID);
	ShaderGraphAsset loaded{};
	if (path.empty() || !FromJson(JsonAdapter::Load(path, false), loaded)) {

		statusMessage_ = "グラフを読み込めませんでした";
		return false;
	}

	selectedAsset_ = assetID;
	ShaderGraphAssetReferences::RefreshSubGraphs(loaded, database);
	graph_ = std::move(loaded);
	history_.Reset(graph_);
	referenceDatabase_ = database;
	referenceStructureRevision_ = database->GetStructureRevision();
	referenceContentRevision_ = database->GetContentRevision();
	previewMaterial_ = {};
	compiledGraphState_.clear();
	previewCompileDirty_ = true;
	++previewRevision_;
	graphLoaded_ = true;
	graphDirty_ = false;
	latestDiagnostics_.clear();
	statusMessage_.clear();
	return true;
}

std::filesystem::path Engine::ShaderGraphEditSession::ResolveCompilePath(const EditorToolContext& context) {

	AssetDatabase* database = context.toolContext.assetDatabase;
	if (!database || !selectedAsset_) {
		return {};
	}
	const std::filesystem::path graphPath = database->ResolveFullPath(selectedAsset_);
	if (graphPath.empty()) {
		statusMessage_ = "グラフのパスを解決できません";
		return {};
	}

	return graphPath;
}

bool Engine::ShaderGraphEditSession::SaveAndCompile(const EditorToolContext& context, const std::filesystem::path& graphPath) {

	AssetDatabase* database = context.toolContext.assetDatabase;
	AssetID savedMaterial{};
	if (!ShaderGraphPublication::CompileAndPublish(
			context, *database, graph_, selectedAsset_, graphPath, savedMaterial, latestDiagnostics_, statusMessage_)) {
		return false;
	}
	graphDirty_ = false;
	statusMessage_ = "コンパイルしました";
	return true;
}

bool Engine::ShaderGraphEditSession::CompilePreview(const EditorToolContext& context) {

	AssetDatabase* database = context.toolContext.assetDatabase;
	compiledGraphState_ = MakeShaderGraphCompileState(graph_);
	previewCompileDirty_ = false;
	if (!database || !selectedAsset_ ||
		!ShaderGraphPublication::CompileAndPublishPreview(
			context, *database, graph_, selectedAsset_, previewMaterial_, latestDiagnostics_, statusMessage_)) {

		return false;
	}
	return true;
}

bool Engine::ShaderGraphEditSession::Save(const EditorToolContext& context) {

	AssetDatabase* database = context.toolContext.assetDatabase;
	if (!database || !selectedAsset_) {
		return false;
	}
	// 保存と索引更新が揃った場合だけ未保存状態を解除する
	const auto path = database->ResolveFullPath(selectedAsset_);
	if (!SaveGraph(*database, graph_, path, selectedAsset_, statusMessage_)) {
		return false;
	}
	graphDirty_ = false;
	statusMessage_ = "保存しました";
	return true;
}

void Engine::ShaderGraphEditSession::Import(ShaderGraphAsset imported) {

	graph_ = std::move(imported);
	history_.Commit(graph_);
	graphDirty_ = true;
	++previewRevision_;
	previewCompileDirty_ = true;
	compiledGraphState_.clear();
	latestDiagnostics_.clear();
	referenceContentRevision_ = UINT64_MAX;
}

void Engine::ShaderGraphEditSession::RefreshReferences(const EditorToolContext& context) {

	const AssetDatabase* database = context.toolContext.assetDatabase;
	if (!graphLoaded_ || !database) return;
	if (referenceDatabase_ == database && referenceStructureRevision_ == database->GetStructureRevision() &&
		referenceContentRevision_ == database->GetContentRevision()) return;
	referenceDatabase_ = database;
	referenceStructureRevision_ = database->GetStructureRevision();
	referenceContentRevision_ = database->GetContentRevision();
	if (ShaderGraphAssetReferences::RefreshSubGraphs(graph_, database)) {
		UpdateCompileState();
	}
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
	referenceContentRevision_ = UINT64_MAX;
	UpdateCompileState();
	return true;
}

bool Engine::ShaderGraphEditSession::Redo() {

	if (!history_.Redo(graph_)) {
		return false;
	}
	referenceContentRevision_ = UINT64_MAX;
	UpdateCompileState();
	return true;
}

void Engine::ShaderGraphEditSession::UpdateCompileState() {

	graphDirty_ = true;
	previewCompileDirty_ = MakeShaderGraphCompileState(graph_) != compiledGraphState_;
	if (previewCompileDirty_) ++previewRevision_;
}
