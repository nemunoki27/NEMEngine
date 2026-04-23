#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Behavior/MonoBehavior.h>
#include <Engine/Core/UUID/TypeHash.h>

// c++
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <cassert>
#include <type_traits>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	BehaviorTypeInfo struct
	//============================================================================

	// ビヘイビアの型情報を保持する構造体
	struct BehaviorTypeInfo {

		// ビヘイビアの名前
		std::string name;
		// ビヘイビアのID
		uint32_t id = 0;
		// C#スクリプトとして登録されているか
		bool managed = false;

		// ビヘイビアのインスタンスを生成する関数
		std::function<std::unique_ptr<MonoBehavior>()> construct;
	};

	//============================================================================
	//	BehaviorTypeRegistry class
	//	ビヘイビアの型情報を管理するクラス
	//============================================================================
	class BehaviorTypeRegistry {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		BehaviorTypeRegistry() = default;
		~BehaviorTypeRegistry() = default;

		// ビヘイビアの型を登録するテンプレート関数
		template <typename T>
		uint32_t Register(const std::string_view& name);
		// C#スクリプトの型を登録
		uint32_t RegisterManaged(const std::string_view& name);

		//--------- accessor -----------------------------------------------------

		const BehaviorTypeInfo& GetInfo(uint32_t id) const;
		const BehaviorTypeInfo* FindByName(const std::string_view& name) const;

		uint32_t GetBehaviorTypeCount() const { return static_cast<uint32_t>(infos_.size()); }

		// シングルトン
		static BehaviorTypeRegistry& GetInstance();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::vector<BehaviorTypeInfo> infos_;
		std::unordered_map<std::string, uint32_t> nameToID_;
		std::unordered_map<uint32_t, uint32_t> typeKeyToID_;
	};

	//============================================================================
	//	BehaviorTypeRegistry macros
	//============================================================================
#define ENGINE_REGISTER_BEHAVIOR(T, NameLiteral) \
	inline const uint32_t kBehID_##T = Engine::BehaviorTypeRegistry::GetInstance().Register<T>(NameLiteral);

	//============================================================================
	//	BehaviorTypeRegistry templateMethods
	//============================================================================

	template <typename T>
	inline uint32_t BehaviorTypeRegistry::Register(const std::string_view& name) {

		static_assert(std::is_base_of_v<MonoBehavior, T>, "T must derive from Engine::Behavior");

		// 既に登録済みならそのIDを返す
		auto it = nameToID_.find(std::string(name));
		if (it != nameToID_.end()) {
			return it->second;
		}

		// 新しい型情報を作成して登録
		BehaviorTypeInfo info{};
		info.name = std::string(name);
		info.id = static_cast<uint32_t>(infos_.size());
		info.managed = false;
		info.construct = []() -> std::unique_ptr<MonoBehavior> { return std::make_unique<T>(); };

		// 追加してIDを返す
		infos_.emplace_back(info);
		nameToID_[info.name] = info.id;
		// ハッシュ値からIDへのマッピングも登録
		typeKeyToID_[EntityToTypeHash(typeid(T).name())] = info.id;
		return info.id;
	}
} // Engine
