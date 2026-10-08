#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Serialization/EntityTreeSnapshot.h>

// c++
#include <string_view>

namespace Engine {

	//============================================================================
	//	EntitySnapshotDuplicator class
	//	複製範囲内のIDと参照を新しい実体へ対応付ける
	//============================================================================
	class EntitySnapshotDuplicator {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 複製先のIDを確定して保存値を作る
		static void Build(const EntityTreeSnapshot& source, const std::string_view& rootName,
			EntityTreeSnapshot& result);
		// 外部親への接続前にルートの親参照を外す
		static void ClearRootParentLink(EntityTreeSnapshot& snapshot);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		// 保存済みの結果へ触れずに複製候補を構築する
		static void BuildCandidate(const EntityTreeSnapshot& sourceSnapshot, const std::string_view& duplicatedRootName,
			EntityTreeSnapshot& outSnapshot);
	};
}
