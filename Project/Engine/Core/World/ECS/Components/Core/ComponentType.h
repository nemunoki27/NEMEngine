#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstdint>
#include <string>
// json
#include <json.hpp>

namespace Engine {

	class ECSWorld;
	struct Entity;

	//============================================================================
	//	ComponentType enums
	//============================================================================
	enum class ComponentStorageKind : uint8_t {

		Data,   // Entityごとに固定長データを持つ
		Buffer, // Entityごとに可変長要素列を持つ
		Tag,    // 値を持たずArchetypeだけを分ける
		Shared, // 同じ値を持つEntity間で共有する
		Chunk,  // Chunk全体で1つの値を持つ
	};

	enum class ComponentWorldDomain : uint8_t {

		Authoring, // 編集用Worldだけで使用する
		Runtime,   // 実行用Worldだけで使用する
		Both,      // 両方のWorldで使用する
	};

	enum class ComponentChangeChannel : uint8_t {

		None = 0,
		Render = 1 << 0,
		Lighting = 1 << 1,
	};

	constexpr ComponentChangeChannel operator|(
		ComponentChangeChannel lhs, ComponentChangeChannel rhs) {

		return static_cast<ComponentChangeChannel>(
			static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
	}

	constexpr ComponentChangeChannel& operator|=(
		ComponentChangeChannel& lhs, ComponentChangeChannel rhs) {

		lhs = lhs | rhs;
		return lhs;
	}

	constexpr bool HasComponentChangeChannel(
		ComponentChangeChannel channels, ComponentChangeChannel target) {

		return (static_cast<uint8_t>(channels) &
			static_cast<uint8_t>(target)) != 0;
	}

	//============================================================================
	//	ComponentType struct
	//	コンポーネントの種類、情報を所持する
	//============================================================================
	struct ComponentTypeInfo {

		// 名前
		std::string name;
		// 一意なID
		uint32_t id = 0;

		// データサイズ、アライメント
		size_t size = 0;
		size_t align = 0;
		// コンポーネントの格納形式
		ComponentStorageKind storageKind = ComponentStorageKind::Data;
		// 使用可能なワールド
		ComponentWorldDomain worldDomain = ComponentWorldDomain::Both;
		// 有効状態をチャンクのビット列で管理するか
		bool enableable = false;
		// Scene/Prefabへ保存するか
		bool serializable = true;
		// 値変更とTransform変更が無効化する抽出キャッシュ
		ComponentChangeChannel changeChannels =
			ComponentChangeChannel::None;
		ComponentChangeChannel transformChannels =
			ComponentChangeChannel::None;
		// Buffer要素のサイズ、アライメント、チャンク内要素数
		size_t elementSize = 0;
		size_t elementAlign = 0;
		uint32_t internalBufferCapacity = 0;
		// 型消去Buffer APIで安全にバイトコピーできる要素か
		bool bufferElementTriviallyCopyable = false;
		// チャンク内の移動、破棄を単純化できる型か
		bool triviallyRelocatable = false;
		bool triviallyDestructible = false;
		// 例外を発生させずに移動できる型か
		bool nothrowMoveConstructible = false;

		// コンストラクタ
		void (*constructDefault)(void* ptr) = nullptr;
		// チャンク外データの初期化
		void (*initializeStorage)(ECSWorld& world, const Entity& entity, void* ptr) = nullptr;
		// Entityへ追加された後の関連Component構築
		void (*onAdded)(ECSWorld& world, const Entity& entity, void* ptr) = nullptr;
		// Entityから削除された後の関連Component破棄
		void (*onRemoved)(ECSWorld& world, const Entity& entity) = nullptr;
		// デストラクタ
		void (*destroy)(void* ptr) = nullptr;
		// コピー、ムーブコンストラクタ
		void (*copyConstruct)(void* dst, const void* src) = nullptr;
		void (*moveConstruct)(void* dst, void* src) = nullptr;
		// チャンク外データの解放
		void (*releaseExternal)(ECSWorld& world, const Entity& entity, void* ptr) = nullptr;

		// json変換関数
		void (*fromJson)(ECSWorld& world, const Entity& entity,
			void* obj, const nlohmann::json& in) = nullptr;
		void (*toJson)(const ECSWorld& world, const Entity& entity,
			const void* obj, nlohmann::json& out) = nullptr;
	};
} // Engine
