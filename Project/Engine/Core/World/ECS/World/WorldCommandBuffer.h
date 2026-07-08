#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>
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
		// 予約済みルートへPrefabをPrefabSystem経由でmaterializeする、assetはUUID
		void EnqueueInstantiatePrefab(const Entity& reservedRoot, const UUID& prefabAsset,
			const Vector3& position, const Quaternion& rotation, bool useTransform, const Entity& parent);
		// Sceneをadditive load / unloadする、instanceはUUID
		void EnqueueLoadSceneAdditive(const UUID& sceneInstanceID, const UUID& sceneAsset);
		void EnqueueUnloadScene(const UUID& sceneInstanceID);
		// Sceneを単一loadする、新sceneをloadしてactiveにし、それまでの全sceneをunloadする
		void EnqueueLoadSceneSingle(const UUID& sceneInstanceID, const UUID& sceneAsset);

		// 予約直後のEntityへのtransform書き込みをstagingする、flush前は実componentが無いため
		// 対象がpending CreateEntity / InstantiatePrefabコマンドに無ければfalseで呼び出し側は通常処理へ
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

		// コマンド種別
		enum class CommandKind : uint8_t {

			DestroyEntity,
			AddComponentByName,
			RemoveComponentByName,
			SetNameEnsuringComponent,
			SetActiveSelfEnsuringComponent,
			SetParent,
			CreateEntity,
			InstantiatePrefab,
			LoadSceneAdditive,
			LoadSceneSingle,
			UnloadScene,
		};

		// transform stagingのどの成分が指定されたか
		enum CommandFlags : uint8_t {

			FlagWorldPositionStays = 1 << 0,
			FlagUseTransform = 1 << 1, // Prefab 生成時に position/rotation を適用するか
			FlagHasPosition = 1 << 2,
			FlagHasRotation = 1 << 3,
			FlagHasScale = 1 << 4,
		};

		// 1コマンド分のデータで値はすべてコピー保持する
		struct Command {

			CommandKind kind;
			Entity target = Entity::Null();
			Entity parent = Entity::Null();
			bool boolValue = false;
			uint8_t flags = 0;
			// Prefab / Sceneのasset、Scene instanceのUUID
			UUID assetID{};
			UUID sceneInstanceID{};
			// CreateEntity / InstantiatePrefabの初期SRTでstagingで確定する
			Vector3 position{};
			Quaternion rotation = Quaternion::Identity();
			Vector3 scale = Vector3::AnyInit(1.0f);
			// AddComponent/RemoveComponent/SetName/CreateEntity(name)用の文字列
			std::string text;
		};

		//--------- variables ----------------------------------------------------

		std::vector<Command> commands_;
		// 予約Entityからpending CreateEntity / InstantiatePrefabコマンドのindexを引くmapで線形走査を避ける
		std::unordered_map<uint64_t, size_t> createCommandIndex_;
		// Flush再入を防ぐ
		bool flushing_ = false;
		// 1回のFlushで許容する最大batch数でコマンドが自分自身を再生産し続ける無限ループを防ぐ
		static constexpr int32_t kMaxFlushBatches = 8;

		//--------- functions ----------------------------------------------------

		// 1コマンドを適用する、適用前にentity/worldを再検証する
		void Apply(ECSWorld& world, const Command& command);
		// 予約Entityをmapキーへ変換する
		static uint64_t EntityKey(const Entity& entity) { return (static_cast<uint64_t>(entity.index) << 32) | entity.generation; }
		// 予約Entityを対象にするpending CreateEntity / InstantiatePrefabコマンドを探す
		Command* FindPendingCreateCommand(const Entity& reserved);
		const Command* FindPendingCreateCommand(const Entity& reserved) const;
	};
} // Engine
