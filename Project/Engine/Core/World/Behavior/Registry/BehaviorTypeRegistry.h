#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Behavior/MonoBehavior.h>
#include <Engine/Core/Foundation/Identity/TypeID.h>

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

		// ビヘイビアの名前（managed は完全修飾型名。表示・legacy 照合用）
		std::string name;
		// ビヘイビアのID（compact runtime type ID。reloadごとに振り直してよい）
		uint32_t id = 0;
		// C#スクリプトとして登録されているか
		bool managed = false;

		// managed scriptの安定識別子（正規化済み GUID 文字列。永続主キー）
		std::string scriptTypeId;
		// 表示名
		std::string displayName;
		// 定義元 .cs パス（drag&drop の source 照合用。永続識別には使わない）
		std::string sourcePath;

		// ビヘイビアのインスタンスを生成する関数
		std::function<std::unique_ptr<MonoBehavior>()> construct;
	};

	//============================================================================
	//	BehaviorTypeRegistry class
	//	ビヘイビアの型情報を管理するクラス
	//============================================================================
	class BehaviorTypeRegistry {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		BehaviorTypeRegistry() = default;
		~BehaviorTypeRegistry() = default;

		// ビヘイビアの型を登録するテンプレート関数
		template <typename T>
		uint32_t Register(const std::string_view& name);
		// C#スクリプトの型を Stable GUID 主キーで登録する
		uint32_t RegisterManaged(const std::string_view& scriptTypeId, const std::string_view& fullName,
			const std::string_view& displayName, const std::string_view& sourcePath);
		// C#スクリプトの型登録をクリア
		void ClearManaged();

		//--------- accessor -----------------------------------------------------

		const BehaviorTypeInfo& GetInfo(uint32_t id) const;
		// Stable Script Type GUID で解決する（runtime 解決の正）
		const BehaviorTypeInfo* FindByStableScriptTypeID(const std::string_view& scriptTypeId) const;
		// 完全修飾型名で解決する（legacy 移行・表示用）
		const BehaviorTypeInfo* FindByName(const std::string_view& name) const;
		// 単純名で解決する（legacy 移行用。複数候補なら曖昧として nullptr）
		const BehaviorTypeInfo* FindManagedBySimpleName(const std::string_view& name) const;
		// 指定 .cs（パス/ファイル名）に定義された managed script 候補を返す（drag&drop用）
		std::vector<const BehaviorTypeInfo*> FindManagedBySourceFile(const std::string_view& sourceFilePath) const;

		uint32_t GetBehaviorTypeCount() const { return static_cast<uint32_t>(infos_.size()); }

		// シングルトン
		static BehaviorTypeRegistry& GetInstance();
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		std::vector<BehaviorTypeInfo> infos_;
		std::unordered_map<std::string, uint32_t> nameToID_;
		// Stable Script Type GUID -> ID（runtime 解決の正）
		std::unordered_map<std::string, uint32_t> guidToID_;
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

