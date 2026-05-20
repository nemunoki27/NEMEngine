#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Graph/GraphTypes.h>

// c++
#include <string>
// imgui
#include <imgui.h>

namespace Engine {

	//============================================================================
	//	NodeGraphStyle class
	//	NodeGraphの色設定を返すクラス
	//============================================================================
	class NodeGraphStyle {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// Node種別に応じたアクセント色を取得する
		ImVec4 GetNodeAccentColor(const std::string& nodeType) const;
		// Pinの値型に応じた色を取得する
		ImVec4 GetPinColor(GraphValueType valueType) const;
		// Linkの値型に応じた色を取得する
		ImVec4 GetLinkColor(GraphValueType valueType) const;
		// Error表示色を取得する
		ImVec4 GetErrorColor() const;
		// Warning表示色を取得する
		ImVec4 GetWarningColor() const;
	};
} // Engine
