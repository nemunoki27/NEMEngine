#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>

// c++
#include <unordered_map>

namespace Engine {

	// front
	class AssetDatabase;

	//============================================================================
	//	AnimationClipManager class
	//	AnimationClipアセットをAssetID単位でパースしキャッシュする
	//============================================================================
	class AnimationClipManager {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		AnimationClipManager() = default;
		~AnimationClipManager() = default;

		// 初回だけパースしキャッシュ済みならそれを返す
		const AnimationClipAsset* GetOrLoad(AssetDatabase& database, AssetID clipID);
		// 指定クリップのキャッシュを破棄する、次回GetOrLoadでファイルから読み直させる、ツール保存後に使う
		void Invalidate(AssetID clipID);
		// キャッシュを破棄する
		void Clear();
	private:
		//============================================================================
		//	private variables
		//============================================================================

		// AssetIDごとのパース済みクリップ
		std::unordered_map<AssetID, AnimationClipAsset> loaded_{};
	};
} // Engine
