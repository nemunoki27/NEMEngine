#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>

// c++
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace Engine {

	class ECSWorld;

	//============================================================================
	//	ManagedScriptExceptionFrame struct
	//	例外の呼出し位置
	//============================================================================
	struct ManagedScriptExceptionFrame {

		std::string method; // 呼出し元の関数
		std::string file;	// 取得できたソースパス
		int32_t line = 0;	// 未取得時は0
		int32_t column = 0; // 未取得時は0
	};

	//============================================================================
	//	ManagedScriptException struct
	//	Scriptの例外と所有者と呼出し位置
	//============================================================================
	struct ManagedScriptException {

		uint64_t exceptionID = 0;  // 受入順の例外番号
		std::string timestamp;	   // HH:MM:SS
		std::string callback;	   // 例外が出たcallback
		uint64_t scriptSlotID = 0; // 所有Scriptの番号
		std::string scriptTypeID;  // Script型の保存ID
		std::string typeName;	   // 表示用の完全修飾型名
		std::string exceptionType; // 例外の型名
		std::string message;	   // 例外メッセージ
		// 一覧から選択する所有Entity
		uint32_t worldIndex = UINT32_MAX;				 // 報告元Worldの番号
		uint32_t worldGeneration = 0;					 // 報告元Worldの世代
		uint32_t entityIndex = UINT32_MAX;				 // 所有Entityの番号
		uint32_t entityGeneration = 0;					 // 所有Entityの世代
		std::string entityName;							 // 報告時点のEntity名
		std::vector<ManagedScriptExceptionFrame> frames; // 呼出し位置の一覧

		// 報告元Worldに生存する所有Entityを返す
		Entity ResolveOwner(const ECSWorld& world) const;
	};

	//============================================================================
	//	ManagedScriptExceptionStore class
	//	Script例外の件数を制限して保持する
	//============================================================================
	class ManagedScriptExceptionStore {
	public:
		//========================================================================
		//	定数
		//========================================================================

		static constexpr size_t kMaxEntries = 256;		  // 保持する例外数の上限
		static constexpr size_t kMaxFrames = 24;		  // 呼出し位置の上限
		static constexpr size_t kMaxMessageLength = 2048; // メッセージの最大長
		static constexpr size_t kMaxStringLength = 512;	  // 型名とパスの最大長

		//========================================================================
		//	public Methods
		//========================================================================

		// C#の診断JSONを読み込み、例外履歴へ追加する
		void ReportJSON(const char* jsonUTF8);
		// 診断が届かなかったcallbackの失敗を記録する
		void ReportFailure() noexcept;

		// 例外履歴を削除する
		void Clear();

		//--------- accessor -----------------------------------------------------

		// 履歴の更新番号
		uint64_t Version() const { return version_; }

		// 履歴とは独立した失敗通知数
		uint64_t ReportSequence() const { return reportSequence_; }

		// 履歴の更新まで保持中の例外を参照する
		const std::deque<ManagedScriptException>& Entries() const { return entries_; }

		size_t Count() const { return entries_.size(); }

		// シングルトン
		static ManagedScriptExceptionStore& GetInstance();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::deque<ManagedScriptException> entries_; // 保存済みの例外履歴
		uint64_t nextID_ = 1;						 // 次の例外番号
		uint64_t version_ = 0;						 // 履歴の更新番号
		uint64_t reportSequence_ = 0;				 // callbackの失敗通知数

		//--------- functions ----------------------------------------------------

		// 上限を超えた古い例外を削除する
		void EnforceBounds();
	};
} // Engine
