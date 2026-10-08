#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/WorldCommand.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <cstddef>
#include <memory>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <tuple>

namespace Engine {

	// 前方宣言
	class ECSWorld;
	class AssetDatabase;
	class SceneInstanceManager;
	class SceneSystem;

	//============================================================================
	//	WorldCommandServices struct
	//============================================================================

	// Scene操作で借用する外部サービス
	struct WorldCommandServices {

		AssetDatabase* assetDatabase = nullptr;			// Scene Assetの解決
		SceneInstanceManager* sceneInstances = nullptr; // Sceneの所属とロード状態
		SceneSystem* sceneSystem = nullptr;				// Scene文書の読み込み
	};

	//============================================================================
	//	WorldCommandBuffer class
	//	構造変更の予約値を保持し、安全地点で順番に適用する
	//============================================================================
	class WorldCommandBuffer {
		friend class ECSWorld;

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
		// 指定したScriptの個体だけを削除する
		void EnqueueRemoveScript(const Entity& entity, const UUID& scriptSlotID);
		// Nameを確保して名前を設定する
		void EnqueueSetNameEnsuringComponent(const Entity& entity, std::string_view name);
		// SceneObjectを確保して有効状態を設定する
		void EnqueueSetActiveSelfEnsuringComponent(const Entity& entity, bool active);
		// 親子関係を変更し、必要ならWorld姿勢を維持する
		void EnqueueSetParent(const Entity& child, const Entity& parent, bool worldPositionStays = false);

		// 初期値を予約し、親子関係を安全地点で確定する
		void EnqueueCreateEntity(ECSWorld& world, const Entity& reserved, std::string_view name, const Entity& parent);
		// Sceneの追加読み込みを予約する
		void EnqueueLoadSceneAdditive(const UUID& sceneInstanceID, AssetID sceneAsset);
		// 指定Sceneの解放を予約する
		void EnqueueUnloadScene(const UUID& sceneInstanceID);
		// 常駐Sceneを残して単一Sceneへ切り替える
		void EnqueueLoadSceneSingle(const UUID& sceneInstanceID, AssetID sceneAsset);

		// 追加前に読み書きできるComponentを予約する
		uint64_t StageAddComponent(ECSWorld& world, const Entity& entity, uint32_t typeID);
		// 予約したComponentの値を取得する
		PendingComponent* FindPendingComponent(const Entity& entity, uint32_t typeID);
		const PendingComponent* FindPendingComponent(const Entity& entity, uint32_t typeID) const;
		// 処理中の追加予約を取消後も保持する
		std::shared_ptr<const PendingComponent> AcquirePendingComponent(const Entity& entity, uint32_t typeID) const;
		// 対象Entityの未適用の値を読み取り用に列挙する
		void CollectPendingComponents(const Entity& entity, std::vector<std::shared_ptr<const PendingComponent>>& out) const;
		// 保存用複製へ未適用Commandを順番どおり渡す
		void CollectUnappliedCommands(std::vector<WorldCommand>& out) const;

		//--------- flush --------------------------------------------------------

		// 予約を順番に適用し、新しい予約は次のbatchへ回す
		void Flush(ECSWorld& world);
		// 未処理の予約と値を破棄する
		void Clear();

		//--------- accessor -----------------------------------------------------

		bool IsEmpty() const { return commands_.empty() && activeCommandIndex_ == activeBatch_.size(); }

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// Entityの世代と型で追加予約を区別する
		using ComponentKey = std::tuple<uint32_t, uint32_t, uint32_t>;

		//--------- variables ----------------------------------------------------

		// 1回のFlushで適用するbatch数の上限
		static constexpr int32_t kMaxFlushBatches = 8;
		// 追加直後に読み書きできる予約値
		std::map<ComponentKey, std::shared_ptr<PendingComponent>> pendingComponents_;
		// 次の安全地点で適用する予約
		std::vector<WorldCommand> commands_;
		// 失敗後も未処理のCommandを保持する
		std::vector<WorldCommand> activeBatch_;
		// 適用中のbatchの次の予約
		size_t activeCommandIndex_ = 0;
		// Flush再入を防ぐ
		bool flushing_ = false;

		//--------- functions ----------------------------------------------------

		// 適用を開始する予約を索引から外す
		void RemovePendingComponent(const WorldCommand& command);
		// 適用済みまたは取消済みの予約値を解放する
		void CancelPendingComponent(const Entity& entity, uint32_t typeID);
	};
}
