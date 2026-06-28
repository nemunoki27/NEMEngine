#pragma once

//============================================================================
//	include
//============================================================================
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	ManagedScriptExceptionFrame struct
	//	script例外のstack frame 1件分
	//============================================================================
	struct ManagedScriptExceptionFrame {

		std::string method;   // 例 GameScripts.Player.Update
		std::string file;     // 定義元 .cs パス、取得できなければ空
		int32_t line = 0;     // 1 始まり、取得できなければ 0
		int32_t column = 0;   // 1 始まり、取得できなければ 0
	};

	//============================================================================
	//	ManagedScriptException struct
	//	script callbackで送出された未処理例外1件分の構造化情報
	//============================================================================
	struct ManagedScriptException {

		uint64_t id = 0;             // 受け入れ順の単調増加 ID
		std::string timestamp;       // HH:MM:SS
		std::string callback;        // Update 等、例外が出た lifecycle callback
		uint64_t scriptSlotID = 0;   // runtime の script slot id、owner Entity 内で entry を一意化
		std::string scriptTypeID;    // Stable Script Type GUID、解決できれば canonical identity
		std::string typeName;        // 完全修飾型名、表示用
		std::string exceptionType;   // System.NullReferenceException 等
		std::string message;         // 例外メッセージ
		// owner entityのnative handle、UIでentity選択する際の解決元
		uint32_t entityIndex = 0;
		uint32_t entityGeneration = 0;
		std::string entityName;      // 報告時点の entity 名、表示用で識別には使わない
		std::vector<ManagedScriptExceptionFrame> frames; // stack frame、上限まで
	};

	//============================================================================
	//	ManagedScriptExceptionStore class
	//	script callback例外をboundedに蓄積するstore
	//============================================================================
	class ManagedScriptExceptionStore {
	public:
		//========================================================================
		//	bounds
		//========================================================================

		static constexpr size_t kMaxEntries = 256;        // 保持する例外 entry の総数上限
		static constexpr size_t kMaxFrames = 24;          // 1 例外あたりの stack frame 上限
		static constexpr size_t kMaxMessageLength = 2048; // message の最大長
		static constexpr size_t kMaxStringLength = 512;   // 型名やパス等の最大長

		//========================================================================
		//	public Methods
		//========================================================================

		// C#から渡されたJSON DTOをparseして1件追加する、parse不能なら何もせず呼び出し側のConsole logは従来どおり残る
		void ReportJson(const char* jsonUtf8);

		// 全entryを破棄する、Play終了時や明示クリア時に呼ぶ
		void Clear();

		//--------- accessor -----------------------------------------------------

		// 内容が変わるたびに増えるversion、UIが再描画要否を判断するのに使う
		uint64_t Version() const { return version_; }

		// 保持中の例外を古い順で返す、main threadからの読み取り専用参照
		const std::deque<ManagedScriptException>& Entries() const { return entries_; }

		size_t Count() const { return entries_.size(); }

		// シングルトン
		static ManagedScriptExceptionStore& GetInstance();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// 上限超過分を古い側から間引く
		void EnforceBounds();

		//--------- variables ----------------------------------------------------

		std::deque<ManagedScriptException> entries_;
		uint64_t nextID_ = 1;
		uint64_t version_ = 0;
	};
} // Engine
