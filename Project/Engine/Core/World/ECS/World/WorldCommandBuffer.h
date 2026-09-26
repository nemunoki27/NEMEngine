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
#include <map>
#include <tuple>

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

		// 初期Componentを予約し、安全地点でScene所属と親子関係を確定する
		void EnqueueCreateEntity(ECSWorld& world, const Entity& reserved, std::string_view name, const Entity& parent);
		// Sceneをadditive load / unloadする、instanceはUUID
		void EnqueueLoadSceneAdditive(const UUID& sceneInstanceID, AssetID sceneAsset);
		void EnqueueUnloadScene(const UUID& sceneInstanceID);
		// Sceneを単一loadする、新sceneをloadしてactiveにし、それまでの全sceneをunloadする
		void EnqueueLoadSceneSingle(const UUID& sceneInstanceID, AssetID sceneAsset);

		// 追加前に読み書きできるComponentを予約する
		uint64_t StageAddComponent(ECSWorld& world, const Entity& entity, uint32_t typeID);
		// 予約したComponentの値を取得する
		PendingComponent* FindPendingComponent(const Entity& entity, uint32_t typeID) const;

		//--------- flush --------------------------------------------------------

		// 積まれたコマンドを適用する、Flush中に積まれたコマンドは次batchへ回す
		void Flush(ECSWorld& world);
		// 未処理コマンドをworld破棄時などに破棄する
		void Clear();

		//--------- accessor -----------------------------------------------------

		bool IsEmpty() const { return commands_.empty() && activeCommandIndex_ == activeBatch_.size(); }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- types --------------------------------------------------------

		//--------- variables ----------------------------------------------------

		// Entityの世代と型で追加予約を区別する
		using ComponentKey = std::tuple<uint32_t, uint32_t, uint32_t>;
		std::map<ComponentKey, std::shared_ptr<PendingComponent>> pendingComponents_;
		std::vector<WorldCommand> commands_;
		// 失敗後も未処理のCommandを保持する
		std::vector<WorldCommand> activeBatch_;
		size_t activeCommandIndex_ = 0;
		// Flush再入を防ぐ
		bool flushing_ = false;
		// 1回のFlushで許容する最大batch数でコマンドが自分自身を再生産し続ける無限ループを防ぐ
		static constexpr int32_t kMaxFlushBatches = 8;

		//--------- functions ----------------------------------------------------

		// 適用を開始する予約を索引から外す
		void RemovePendingComponent(const WorldCommand& command);

	};
} // Engine
