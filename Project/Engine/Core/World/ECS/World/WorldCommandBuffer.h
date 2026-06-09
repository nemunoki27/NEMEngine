#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>

// c++
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Engine {

	// front
	class ECSWorld;

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
		// 親子付け
		void EnqueueSetParent(const Entity& child, const Entity& parent);

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
		// 指示書(02)が挙げる InstantiatePrefab / LoadSceneAdditive / UnloadScene は、
		// それらを発火するC# Prefab/Scene gameplay APIが 07_csharp_gameplay_api.md で導入される。
		// 現時点でenqueueする経路が無いため、未接続の死蔵コードを作らないよう本enumには含めない。
		// 追加時はここにkindを足し、Apply()へcaseとEnqueue系APIを足すだけで拡張できる設計にしている。
		enum class CommandKind : uint8_t {

			DestroyEntity,
			AddComponentByName,
			RemoveComponentByName,
			SetNameEnsuringComponent,
			SetActiveSelfEnsuringComponent,
			SetParent,
		};

		// 1コマンド分のデータ。値はすべてコピー保持する
		struct Command {

			CommandKind kind;
			Entity target = Entity::Null();
			Entity parent = Entity::Null();
			bool boolValue = false;
			// AddComponent/RemoveComponent/SetName用の文字列
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
	};
} // Engine
