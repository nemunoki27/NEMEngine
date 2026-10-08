#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ScriptProfiler.h>

// c++
#include <Engine/Core/Scripting/Managed/Diagnostics/ScriptProfileSummary.h>

#include <map>
#include <vector>

namespace Engine {

	//============================================================================
	//	ScriptProfilerTool class
	//	スクリプトのコールバックと選択した処理区間を表示する
	//============================================================================
	class ScriptProfilerTool : public IEditorTool {
	public:
		void OpenEditorTool() override;
		void DrawEditorTool(const EditorToolContext& context) override;
		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }

	private:

		// 履歴を表示用の小さな行へ集計する
		void RefreshRows();
		// 一覧と区間ツリーを同じ列構成で描画する
		void DrawRows(bool detail, int32_t parent = -1);
		// 対象と計測状態を変更して履歴を更新する
		void Configure(bool enabled, const std::string& typeName, uint64_t ownerID);

		ToolDescriptor descriptor_{
			.id = "engine.script_profiler",
			.name = "スクリプトプロファイラー",
			.category = "スクリプト",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::AllowPlayMode,
			.order = 1,
		};
		std::vector<ScriptProfileSummary> rows_;
		std::map<int32_t, std::vector<size_t>> detailChildren_;
		char filter_[128]{};
		double lastRefresh_ = -1;
		int sort_ = 1;
		bool instances_ = false;
		bool openWindow_ = false;
	};
}
