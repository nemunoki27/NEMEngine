#pragma once

//============================================================================
//	include
//============================================================================
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace Engine {

	//============================================================================
	//	ScriptExecutionOrderTable class
	//	Script Type GUID単位の実行順を保持するprojectレベル設定
	//============================================================================
	class ScriptExecutionOrderTable {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// UI表示と編集用のエントリ
		struct Entry {

			std::string scriptTypeID;
			std::string displayName;
			int32_t executionOrder = 0;
		};

		//--------- functions ----------------------------------------------------

		// 未ロードならProjectSettings/ScriptExecutionOrder.jsonを読む、冪等
		void EnsureLoaded();
		// 強制的に再読込する、設定変更やproject load時
		void Reload();

		// 指定scriptTypeIDの実行順を返す、未登録は0でoverride有無は区別しない
		int32_t GetOrder(const std::string_view& scriptTypeID) const;

		// overrideが登録されているときのみtrueを返しoutOrderへ値を入れる、override無しと明示的に0を設定した状態を区別するUIとReset用
		bool TryGetOverride(const std::string_view& scriptTypeID, int32_t& outOrder) const;

		// 実行順を設定する、UI編集用でdisplayNameは表示補助
		void SetOrder(const std::string_view& scriptTypeID, const std::string_view& displayName, int32_t order);
		// エントリを削除する
		void Remove(const std::string_view& scriptTypeID);
		// temp fileとrenameで原子的に保存する
		bool Save() const;

		//--------- accessor -----------------------------------------------------

		// UI用のエントリ一覧、scriptTypeIDでソート済み
		const std::vector<Entry>& GetEntries() const { return entries_; }

		// シングルトン
		static ScriptExecutionOrderTable& GetInstance();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		void RebuildLookup();
		static std::string SettingsPath();

		//--------- variables ----------------------------------------------------

		std::unordered_map<std::string, int32_t> orderByGuid_;
		std::vector<Entry> entries_;
		bool loaded_ = false;
	};
} // Engine
