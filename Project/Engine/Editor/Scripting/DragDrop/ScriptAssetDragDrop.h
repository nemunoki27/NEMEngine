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

		// Scriptアセットから解決した型で永続主キーはscriptTypeIdのGUID、
		// typeNameは表示とlastKnownTypeName用の完全修飾名
		struct ResolvedScriptType {

			std::string scriptTypeId;
			std::string typeName;
		};

		// .cs の Scriptアセットをmanifestのsource情報経由で型へ解決する
		// ファイル名stemをクラス名と見なす旧方式は使わず、候補が一意でなければ失敗扱いにする
		bool ResolveScriptType(const EditorPanelContext& context, AssetID assetID, ResolvedScriptType& outType);
		// Projectパネルからのドラッグ&ドロップを受け取り、解決した型を返す
		bool AcceptScriptAssetDrop(const EditorPanelContext& context, AssetID& outAssetID, ResolvedScriptType& outType);
	}
} // Engine
