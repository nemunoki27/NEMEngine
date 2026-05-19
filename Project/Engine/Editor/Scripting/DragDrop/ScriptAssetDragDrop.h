#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <string>

namespace Engine {

	//============================================================================
	//	ScriptAssetDragDrop
	//============================================================================
	namespace ScriptAssetDragDrop {

		// ScriptアセットからC#側の型名を解決する
		bool ResolveScriptTypeName(const EditorPanelContext& context, AssetID assetID, std::string& outTypeName);
		// Projectパネルからのドラッグ&ドロップを受け取り、C#側の型名を返す
		bool AcceptScriptAssetDrop(const EditorPanelContext& context, AssetID& outAssetID, std::string& outTypeName);
	}
} // Engine
