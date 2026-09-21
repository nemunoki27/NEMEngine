#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/WorldCommand.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>

// c++
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Engine {

	// front
	class ECSWorld;
	class AssetDatabase;
	class SceneInstanceManager;
	class SceneSystem;

	//============================================================================
	//	WorldCommandServices struct
	//============================================================================

	// Prefab/SceneコマンドのFlush適用時に必要となる外部サービス
	struct WorldCommandServices {

		AssetDatabase* assetDatabase = nullptr;
		SceneInstanceManager* sceneInstances = nullptr;
		SceneSystem* sceneSystem = nullptr;
	};

	//============================================================================
	//	WorldCommandBuffer class
	//	scripting由来の構造変更を安全地点までキューに積んで遅延適用する
	//============================================================================
	class WorldCommandBuffer {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		WorldCommandBuffer() = default;
		~WorldCommandBuffer() = default;

		//--------- enqueue ------------------------------------------------------

		// エンティティ破棄
		void EnqueueDestroyEntity(const Entity& entity);
		// 型名でコンポーネント追加/削除
		void EnqueueAddComponentByName(const Entity& entity, std::string_view typeName);
		void EnqueueRemoveComponentByName(const Entity& entity, std::string_view typeName);
		// 名前設定でNameComponentが無ければ追加してから設定する
		void EnqueueSetNameEnsuringComponent(const Entity& entity, std::string_view name);
		// アクティブ設定でSceneObjectComponentが無ければ追加してから設定する
		void EnqueueSetActiveSelfEnsuringComponent(const Entity& entity, bool active);
		// 親子付けでworldPositionStays=trueなら親変更前後でworld transformを維持する
		void EnqueueSetParent(const Entity& child, const Entity& parent, bool worldPositionStays = false);

		// 予約済みEntityをmaterializeしてTransform/SceneObject/Nameを付与しstaged SRTとparentを適用する
		void EnqueueCreateEntity(const Entity& reserved, std::string_view name, const Entity& parent);
		// Sceneをadditive load / unloadする、instanceはUUID
		void EnqueueLoadSceneAdditive(const UUID& sceneInstanceID, AssetID sceneAsset);
		void EnqueueUnloadScene(const UUID& sceneInstanceID);
		// Sceneを単一loadする、新sceneをloadしてactiveにし、それまでの全sceneをunloadする
		void EnqueueLoadSceneSingle(const UUID& sceneInstanceID, AssetID sceneAsset);

		// 予約直後のEntityへのtransform書き込みをstagingする、flush前は実componentが無いため
		// 対象がpending CreateEntityコマンドに無ければfalseで呼び出し側は通常処理へ
		bool StageCreatePosition(const Entity& reserved, const Vector3& position);
		bool StageCreateRotation(const Entity& reserved, const Quaternion& rotation);
		bool StageCreateScale(const Entity& reserved, const Vector3& scale);
		// 対象が予約中の未materialize Entityか
		bool IsPendingCreate(const Entity& reserved) const;

		//--------- flush --------------------------------------------------------

		// 積まれたコマンドを適用する、Flush中に積まれたコマンドは次batchへ回す
		void Flush(ECSWorld& world);
		// 未処理コマンドをworld破棄時などに破棄する
		void Clear();

		//--------- accessor -----------------------------------------------------

		bool IsEmpty() const { return commands_.empty(); }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- types --------------------------------------------------------

		//--------- variables ----------------------------------------------------

		std::vector<WorldCommand> commands_;
		// 予約Entityからpending CreateEntityコマンドのindexを引くmapで線形走査を避ける
		std::unordered_map<uint64_t, size_t> createCommandIndex_;
		// Flush再入を防ぐ
		bool flushing_ = false;
		// 1回のFlushで許容する最大batch数でコマンドが自分自身を再生産し続ける無限ループを防ぐ
		static constexpr int32_t kMaxFlushBatches = 8;

		//--------- functions ----------------------------------------------------

		// 予約Entityをmapキーへ変換する
		static uint64_t EntityKey(const Entity& entity) { return (static_cast<uint64_t>(entity.index) << 32) | entity.generation; }
		// 予約Entityを対象にするpending CreateEntityコマンドを探す
		WorldCommand* FindPendingCreateCommand(const Entity& reserved);
		const WorldCommand* FindPendingCreateCommand(const Entity& reserved) const;
	};
} // Engine
