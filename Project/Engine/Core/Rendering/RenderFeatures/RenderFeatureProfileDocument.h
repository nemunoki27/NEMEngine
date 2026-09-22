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
		void Read();
		// 編集内容を保存する
		bool Save() const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		friend class RenderFeatureProfileService;

		bool loaded_ = false;
		bool dirty_ = false;
		std::filesystem::path profilePath_{};
		RenderFeatureProfileAsset profile_{};
	};
}
