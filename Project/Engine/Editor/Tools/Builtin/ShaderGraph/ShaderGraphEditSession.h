#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphHistory.h"
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphIR.h>

// c++
#include <filesystem>

namespace Engine {

	struct EditorToolContext;

	//============================================================================
	//	ShaderGraphEditSession class
	//	Graphの編集内容と履歴と成果物の対応を保持するクラス
	//============================================================================
	class ShaderGraphEditSession {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		bool Load(const EditorToolContext& context, AssetID assetID);
		void Save(const EditorToolContext& context);
		// 位置の確定前に保存先を検証する
		std::filesystem::path ResolveCompilePath(const EditorToolContext& context);
		bool SaveAndCompile(const EditorToolContext& context, const std::filesystem::path& graphPath);
		void Import(ShaderGraphAsset imported);
		void CaptureHistory();
		void Commit();
		bool Undo();
		bool Redo();
		void MarkDirty(bool changed = true) { graphDirty_ |= changed; }

		//--------- accessor -----------------------------------------------------

		ShaderGraphAsset& GetDraft() { return graph_; }
		const ShaderGraphAsset& GetDraft() const { return graph_; }
		AssetID GetAssetID() const { return selectedAsset_; }
		AssetID GetPreviewMaterialID() const { return previewMaterial_; }
		bool IsLoaded() const { return graphLoaded_; }
		bool NeedsCompile() const { return previewCompileDirty_; }
		bool CanUndo() const { return history_.CanUndo(); }
		bool CanRedo() const { return history_.CanRedo(); }
		const std::vector<ShaderGraphDiagnostic>& GetDiagnostics() const { return latestDiagnostics_; }
		std::string& GetStatusMessage() { return statusMessage_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		AssetID selectedAsset_{};
		AssetID previewMaterial_{};
		ShaderGraphAsset graph_{};
		ShaderGraphHistory history_{};
		bool graphLoaded_ = false;
		bool graphDirty_ = false;
		bool previewCompileDirty_ = false;
		std::string compiledGraphState_{};
		std::vector<ShaderGraphDiagnostic> latestDiagnostics_{};
		std::string statusMessage_{};

		//--------- functions ----------------------------------------------------

		// 編集内容とコンパイル済み状態を照合する
		void UpdateCompileState();
	};
}
