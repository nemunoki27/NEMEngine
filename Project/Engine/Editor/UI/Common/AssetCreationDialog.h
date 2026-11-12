#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Assets/Project/ProjectAssetFileTypes.h>

// c++
#include <functional>
#include <string>

namespace Engine {

	//============================================================================
	//	AssetCreationDialog class
	//	保存先を確認して専用アセットを作成する共通画面
	//============================================================================
	class AssetCreationDialog {
	public:
		// 作成画面を開く
		void Open();
		// 保存先の検証後にツール固有の作成処理を呼ぶ
		void Draw(const char* title, ProjectAssetFileKind kind,
			const std::function<bool(const std::string&, std::string&)>& create);
	private:
		// 作成入力と直前の失敗を保持する
		std::string name_;
		std::string diagnostic_;
		bool openRequested_ = false;
	};
}
