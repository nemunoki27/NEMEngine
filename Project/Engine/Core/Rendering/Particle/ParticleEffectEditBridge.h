#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>

// c++
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	ParticleEffectEditBridge class
	//	エディターの編集内容を保存なしで即ランタイムへ反映するための橋渡し
	//============================================================================
	class ParticleEffectEditBridge {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 編集内容を送る、バージョンを進めてランタイム側の再構築を促す
		void Push(AssetID assetID, const ParticleEffectAsset& asset);

		// 上書きを解除する、以後はファイルの内容へ戻る
		void Remove(AssetID assetID);

		//--------- accessor -----------------------------------------------------

		// 上書きが更新されていれば取得する、取得できたらlastAppliedVersionを進める
		bool TryConsume(AssetID assetID, uint64_t& lastAppliedVersion, ParticleEffectAsset& outAsset) const;

		// シングルトン
		static ParticleEffectEditBridge& GetInstance();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// 上書き1件分
		struct Entry {

			uint64_t version = 0;
			ParticleEffectAsset asset{};
		};

		//--------- variables ----------------------------------------------------

		// アセットIDから上書きへのマップ
		std::unordered_map<AssetID, Entry> entries_;
	};
} // Engine
