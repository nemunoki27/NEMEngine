#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>

namespace Engine {

	//============================================================================
	//	ScriptBuildDiagnosticsTool class
	//	Compiler Error List、structured build diagnostic storeとbuild snapshotを読む
	//============================================================================
	class ScriptBuildDiagnosticsTool :
		public IEditorTool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ScriptBuildDiagnosticsTool() = default;
		~ScriptBuildDiagnosticsTool() override = default;

		void OpenEditorTool() override;
		void DrawEditorTool(const EditorToolContext& context) override;

		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// 一覧ウィンドウを描画する
		void DrawWindow(const EditorToolContext& context);

		//--------- variables ----------------------------------------------------

		ToolDescriptor descriptor_{
			.id = "engine.script_build_diagnostics",
			.name = "Compiler Error List",
			.category = "Scripting",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::AllowPlayMode,
			.order = 0,
		};

		bool openWindow_ = false;
		bool showErrors_ = true;
		bool showWarnings_ = true;
		bool latestBuildOnly_ = true;
		char textFilter_[128]{};
	};
} // Engine
