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

		// 内容の更新後に読込を試み、失敗時は旧Clipを返す
		const AnimationClipAsset* GetOrLoad(AssetDatabase& database, AssetID clipID);
		// 指定Clipを次回取得時に読み直す
		void Invalidate(AssetID clipID);
		// キャッシュを破棄する
		void Clear();
		uint64_t GetRevision() const { return revision_; }
	private:
		//============================================================================
		//	private variables
		//============================================================================

		struct CacheEntry {

			AnimationClipAsset clip;
			std::filesystem::path path;
			uint64_t contentRevision = UINT64_MAX;
			uint64_t structureRevision = UINT64_MAX;
			bool attempted = false;
			bool valid = false;
		};
		uint64_t revision_ = 0;

		// AssetIDごとの公開Clipと読込状態
		std::unordered_map<AssetID, CacheEntry> loaded_{};
	};
} // Engine
