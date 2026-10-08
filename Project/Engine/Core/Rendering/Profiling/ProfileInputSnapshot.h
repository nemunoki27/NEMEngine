#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstdint>
#include <string>

namespace Engine {

	class AssetDatabase;
	class ECSWorld;
	class SceneInstanceManager;

	// 計測開始時の入力内容
	struct ProfileInputSnapshot {

		std::string assetSHA256;
		std::string worldSHA256;
		uint64_t fileCount = 0;
		uint64_t entityCount = 0;
		uint64_t assetStructureRevision = 0;
		uint64_t assetContentRevision = 0;
		bool complete = false;
	};

	//============================================================================
	//	ProfileInputSnapshotBuilder class
	//	描画入力を変更せず比較用の識別情報を作る
	//============================================================================
	class ProfileInputSnapshotBuilder {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ProfileInputSnapshotBuilder() = delete;
		~ProfileInputSnapshotBuilder() = delete;

		// Assetと開始時のComponent値を取得する
		static ProfileInputSnapshot Capture(const AssetDatabase& database, const ECSWorld& world,
			const SceneInstanceManager* scenes);
		// 保存処理を呼ばずWorldの現在値を取得する
		static ProfileInputSnapshot CaptureWorld(const ECSWorld& world, const SceneInstanceManager* scenes);
		// 記録中にAssetの索引や内容が変わっていないか確認する
		static bool HasSameAssetRevisions(const AssetDatabase& database, const ProfileInputSnapshot& snapshot);
	};
}
