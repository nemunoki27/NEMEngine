#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>

namespace Engine {

	//============================================================================
	//	ScriptProfilerTool class
	//	managed script callbackのdetail profiler、typeとentityとslotとcallback単位
	//============================================================================
	class ScriptProfilerTool :
		public IEditorTool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ScriptProfilerTool() = default;
		~ScriptProfilerTool() override = default;

		void OpenEditorTool() override;
		void DrawEditorTool(const EditorToolContext& context) override;

		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// 集計ウィンドウを描画する
		void DrawWindow(const EditorToolContext& context);

		//--------- variables ----------------------------------------------------

		ToolDescriptor descriptor_{
			.id = "engine.script_profiler",
			.name = "Script Profiler",
			.category = "Scripting",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::AllowPlayMode,
			.order = 0,
		};

		bool openWindow_ = false;
		bool onlySelectedEntity_ = false;
		int topN_ = 20;
		// 並べ替え基準、0はtotal 1はmax 2はcallCount
		int sortMode_ = 0;
	};
} // Engine
