#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>

// c++
#include <filesystem>
#include <memory>
#include <unordered_map>

namespace Engine {

	class AssetDatabase;

	using PrefabBaseEntities = std::unordered_map<UUID, PrefabBaseEntity>;

	//============================================================================
	//	PrefabBaseCache class
	//	比較中の基準データを所有し、読込結果を保持する
	//============================================================================
	class PrefabBaseCache {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		PrefabBaseCache() = default;
		~PrefabBaseCache() = default;

		// 更新後の基準を読み取り、失敗時は空の参照を返す
		std::shared_ptr<const PrefabBaseEntities> Load(AssetDatabase& database, AssetID prefabAsset);
		// 所有を解除し、取得済みの基準は利用者が保持する
		void Clear();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		AssetID assetID_{};
		const AssetDatabase* database_ = nullptr;
		std::filesystem::path sourcePath_;
		std::filesystem::file_time_type writeTime_{};
		uintmax_t fileSize_ = 0;
		uint64_t structureRevision_ = 0;
		uint64_t contentRevision_ = 0;
		std::shared_ptr<const PrefabBaseEntities> base_;
	};
}
