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

		// Scriptアセットから解決した型。永続主キーは scriptTypeId（GUID）で、
		// typeName は表示・lastKnownTypeName 用の完全修飾名。
		struct ResolvedScriptType {

			std::string scriptTypeId;
			std::string typeName;
		};

		// Scriptアセット（.cs）を manifest の source 情報経由で型へ解決する。
		// ファイル名 stem をクラス名と見なす旧方式は使わず、候補が一意でなければ失敗扱いにする。
		bool ResolveScriptType(const EditorPanelContext& context, AssetID assetID, ResolvedScriptType& outType);
		// Projectパネルからのドラッグ&ドロップを受け取り、解決した型を返す
		bool AcceptScriptAssetDrop(const EditorPanelContext& context, AssetID& outAssetID, ResolvedScriptType& outType);
	}
} // Engine
