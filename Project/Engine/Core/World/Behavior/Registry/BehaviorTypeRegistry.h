#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Behavior/MonoBehavior.h>

// c++
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <cassert>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	BehaviorTypeInfo struct
	//============================================================================
	// ビヘイビアの型情報を保持する構造体
	struct BehaviorTypeInfo {

		// ビヘイビアの名前、managedは完全修飾型名を表示に使う
		std::string name;
		// ビヘイビアのIDはcompact runtime type IDでreloadごとに振り直してよい
		uint32_t id = 0;
		// C#スクリプトとして登録されているか
		bool managed = false;

		// managed scriptの安定識別子で正規化済みGUID文字列の永続主キー
		std::string scriptTypeID;
		// 表示名
		std::string displayName;
		// 定義元.csパスでdrag&dropのsource照合用、永続識別には使わない
		std::string sourcePath;
		// [DefaultExecutionOrder]の既定実行順でEditor overrideが無いときのdefault、未指定は0
		int32_t defaultExecutionOrder = 0;
		// ProjectSettingsの上書きを反映した実行順
		int32_t executionOrder = 0;

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

		// C#スクリプトの型をStable GUID主キーで登録する
		uint32_t RegisterManaged(const std::string_view& scriptTypeID, const std::string_view& fullName,
			const std::string_view& displayName, const std::string_view& sourcePath, int32_t defaultExecutionOrder = 0);
		// C#スクリプトの型登録をクリア
		void ClearManaged();

		//--------- accessor -----------------------------------------------------

		const BehaviorTypeInfo& GetInfo(uint32_t id) const;
		// Stable Script Type GUIDで解決するruntime解決の正
		const BehaviorTypeInfo* FindByStableScriptTypeID(const std::string_view& scriptTypeID) const;
		// 完全修飾型名で解決する
		const BehaviorTypeInfo* FindByName(const std::string_view& name) const;
		// 指定.csのパスやファイル名に定義されたmanaged script候補をdrag&drop用に返す
		std::vector<const BehaviorTypeInfo*> FindManagedBySourceFile(const std::string_view& sourceFilePath) const;
		// ProjectSettingsの上書き値からManaged Scriptの実行順を更新する
		void RefreshManagedExecutionOrders();

		uint32_t GetBehaviorTypeCount() const { return static_cast<uint32_t>(infos_.size()); }
		uint64_t GetExecutionOrderRevision() const { return executionOrderRevision_; }

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
		// 実行順変更をBehaviorSystemのソート済みキャッシュへ通知する
		uint64_t executionOrderRevision_ = 1;
	};
} // Engine
