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

		// ビヘイビアの名前、managedは完全修飾型名で表示とlegacy照合に使う
		std::string name;
		// ビヘイビアのIDはcompact runtime type IDでreloadごとに振り直してよい
		uint32_t id = 0;
		// C#スクリプトとして登録されているか
		bool managed = false;

		// managed scriptの安定識別子で正規化済みGUID文字列の永続主キー
		std::string scriptTypeId;
		// 表示名
		std::string displayName;
		// 定義元.csパスでdrag&dropのsource照合用、永続識別には使わない
		std::string sourcePath;
		// [DefaultExecutionOrder]の既定実行順でEditor overrideが無いときのdefault、未指定は0
		int32_t defaultExecutionOrder = 0;

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
		// C#スクリプトの型をStable GUID主キーで登録する
		uint32_t RegisterManaged(const std::string_view& scriptTypeId, const std::string_view& fullName,
			const std::string_view& displayName, const std::string_view& sourcePath, int32_t defaultExecutionOrder = 0);
		// C#スクリプトの型登録をクリア
		void ClearManaged();

		//--------- accessor -----------------------------------------------------

		const BehaviorTypeInfo& GetInfo(uint32_t id) const;
		// Stable Script Type GUIDで解決するruntime解決の正
		const BehaviorTypeInfo* FindByStableScriptTypeID(const std::string_view& scriptTypeId) const;
		// 完全修飾型名で解決するlegacy移行と表示用
		const BehaviorTypeInfo* FindByName(const std::string_view& name) const;
		// 単純名で解決するlegacy移行用で複数候補なら曖昧としてnullptr
		const BehaviorTypeInfo* FindManagedBySimpleName(const std::string_view& name) const;
		// 指定.csのパスやファイル名に定義されたmanaged script候補をdrag&drop用に返す
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
		// Stable Script Type GUIDからIDへ、runtime解決の正
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

