#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleGPUWorldInputs.h"
#include <Engine/Core/Assets/Async/AssetWorkerPool.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>

// c++
#include <atomic>
#include <filesystem>

namespace Engine {

	// FGAとHeightmapの不変データを内容Revisionごとに非同期で保持する
	class ParticleFieldAssetCache {
	public:

		ParticleFieldAssetCache();
		~ParticleFieldAssetCache();
		ParticleGPUWorldLoaders MakeLoaders(const AssetDatabase& database);
		std::string GetDiagnostic(AssetID asset) const;
	private:

		struct Result {

			std::atomic<bool> ready{false};
			std::shared_ptr<const VectorFieldData> vector;
			std::shared_ptr<const ParticleHeightmapData> height;
			std::string diagnostic;
		};
		struct Request {

			std::filesystem::path path;
			std::shared_ptr<Result> result;
			bool heightmap = false;
		};
		struct Entry {

			uint64_t revision = UINT64_MAX;
			uint64_t structureRevision = UINT64_MAX;
			std::shared_ptr<Result> result;
			std::shared_ptr<Result> retained;
		};

		AssetWorkerPool<Request> workers_;
		std::unordered_map<AssetID, Entry> vectors_;
		std::unordered_map<AssetID, Entry> heights_;
		std::weak_ptr<const uint8_t> databaseLifetime_;

		// DBの主スレッドで世代とパスを固定してから読込要求を積む
		std::shared_ptr<Result> RequestAsset(const AssetDatabase& database, AssetID asset, bool heightmap);
		// 主スレッドのDBへ触れずファイルから不変データを作る
		static void Decode(Request request);
	};
}
