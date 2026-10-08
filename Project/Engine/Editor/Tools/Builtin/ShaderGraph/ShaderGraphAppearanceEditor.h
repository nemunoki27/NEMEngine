#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphAppearance.h"

// c++
#include <string>

namespace Engine {

	//============================================================================
	//	ShaderGraphAppearanceEditor class
	//	Graphの外観設定と編集を保持する
	//============================================================================
	class ShaderGraphAppearanceEditor {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ShaderGraphAppearanceEditor();

		// 設定を編集しpreviewの再作成要求を返す
		bool Draw(std::string& status);
		// Node Editorへ外観を適用する
		void Apply() const;

		//--------- accessor -----------------------------------------------------

		const ShaderGraphAppearanceSetting& GetSettings() const { return settings_; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ShaderGraphAppearanceSetting settings_{};
	};
} // Engine
