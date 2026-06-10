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
	// Prefab / Scene コマンドの Flush 適用時に必要となる外部サービス。
	// ECSWorld 自身は所有しないため、EngineApplication が毎フレーム active world へ設定する。
	// PrefabSystem / HierarchySystem は state を持たないため Apply 内でローカル生成する。
	struct WorldCommandServices {

		AssetDatabase* assetDatabase = nullptr;
		SceneInstanceManager* sceneInstances = nullptr;
		SceneSystem* sceneSystem = nullptr;
	};

	//============================================================================
	//	WorldCommandBuffer class
	//	scripting由来の構造変更を安全地点までキューに積んで遅延適用する
	//============================================================================
	// BehaviorSystemのForEach走査中にarchetype移動や親子変更を即時反映すると走査を壊すため、
	// component追加削除・親子付け・破棄などの構造変更はこのバッファ経由でFlush時にまとめて適用する。
	// 各コマンドは安定ハンドル(Entity index/generation)と必要値をコピーして保持し、
	// componentへのポインタや参照は一切保持しない。
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
		// 型名でコンポーネント追加 / 削除
		void EnqueueAddComponentByName(const Entity& entity, std::string_view typeName);
		void EnqueueRemoveComponentByName(const Entity& entity, std::string_view typeName);
		// 名前設定（NameComponentが無ければ追加してから設定）
		void EnqueueSetNameEnsuringComponent(const Entity& entity, std::string_view name);
		// アクティブ設定（SceneObjectComponentが無ければ追加してから設定）
		void EnqueueSetActiveSelfEnsuringComponent(const Entity& entity, bool active);
		// 親子付け（worldPositionStays=true なら親変更前後で world transform を維持する）
		void EnqueueSetParent(const Entity& child, const Entity& parent, bool worldPositionStays = false);

		// 予約済み Entity を materialize する（Transform/SceneObject/Name を付与し、staged SRT/parent を適用）
		void EnqueueCreateEntity(const Entity& reserved, std::string_view name, const Entity& parent);
		// 予約済みルートへ Prefab を materialize する（PrefabSystem 経由。asset は UUID）
		void EnqueueInstantiatePrefab(const Entity& reservedRoot, const UUID& prefabAsset,
			const Vector3& position, const Quaternion& rotation, bool useTransform, const Entity& parent);
		// Scene を additive load / unload する（instance は UUID）
		void EnqueueLoadSceneAdditive(const UUID& sceneInstanceID, const UUID& sceneAsset);
		void EnqueueUnloadScene(const UUID& sceneInstanceID);

		// 予約直後の Entity に対する transform 書き込みを staging する（flush 前は実 component が無いため）。
		// 対象が pending CreateEntity / InstantiatePrefab コマンドに無ければ false（呼び出し側は通常処理へ）。
		bool StageCreatePosition(const Entity& reserved, const Vector3& position);
		bool StageCreateRotation(const Entity& reserved, const Quaternion& rotation);
		bool StageCreateScale(const Entity& reserved, const Vector3& scale);
		// 対象が予約中（未 materialize）の Entity か
		bool IsPendingCreate(const Entity& reserved) const;

		//--------- flush --------------------------------------------------------

		// 積まれたコマンドを適用する。Flush中に積まれたコマンドは次batchへ回す
		void Flush(ECSWorld& world);
		// 未処理コマンドを破棄する（world破棄時など）
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
			UnloadScene,
		};

		// transform staging のどの成分が指定されたか
		enum CommandFlags : uint8_t {

			FlagWorldPositionStays = 1 << 0,
			FlagUseTransform = 1 << 1, // Prefab 生成時に position/rotation を適用するか
			FlagHasPosition = 1 << 2,
			FlagHasRotation = 1 << 3,
			FlagHasScale = 1 << 4,
		};

		// 1コマンド分のデータ。値はすべてコピー保持する
		struct Command {

			CommandKind kind;
			Entity target = Entity::Null();
			Entity parent = Entity::Null();
			bool boolValue = false;
			uint8_t flags = 0;
			// Prefab / Scene の asset、Scene instance の UUID
			UUID assetID{};
			UUID sceneInstanceID{};
			// CreateEntity / InstantiatePrefab の初期 SRT（staging で確定）
			Vector3 position{};
			Quaternion rotation = Quaternion::Identity();
			Vector3 scale = Vector3::AnyInit(1.0f);
			// AddComponent/RemoveComponent/SetName/CreateEntity(name)用の文字列
			std::string text;
		};

		//--------- variables ----------------------------------------------------

		std::vector<Command> commands_;
		// Flush再入を防ぐ
		bool flushing_ = false;
		// 1回のFlushで許容する最大batch数（コマンドが自分自身を再生産し続ける無限ループ防止）
		static constexpr int32_t kMaxFlushBatches = 8;

		//--------- functions ----------------------------------------------------

		// 1コマンドを適用する。適用前にentity/worldを再検証する
		void Apply(ECSWorld& world, const Command& command);
		// 予約 Entity を対象にする pending CreateEntity / InstantiatePrefab コマンドを探す
		Command* FindPendingCreateCommand(const Entity& reserved);
		const Command* FindPendingCreateCommand(const Entity& reserved) const;
	};
} // Engine
