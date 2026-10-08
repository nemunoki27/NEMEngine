#pragma once

//============================================================================
//	include
//============================================================================
#include "RenderFeatureProfile.h"
#include <filesystem>

namespace Engine {

	//============================================================================
	//	RenderFeatureProfileDocument class
	//	保存対象のProfileと編集状態を所有する
	//============================================================================
	class RenderFeatureProfileDocument {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 保存先から現在の内容を読み直す
		bool Read(const std::filesystem::path& path);
		// 編集内容を保存する
		bool Save() const;

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		friend class RenderFeatureProfileService;

		//--------- variables ----------------------------------------------------

		bool loaded_ = false;				  // 読込済みの状態
		bool dirty_ = false;				  // 未保存の編集
		std::filesystem::path profilePath_{}; // 編集中の保存先
		RenderFeatureProfileAsset profile_{}; // 編集用の設定
	};
}
