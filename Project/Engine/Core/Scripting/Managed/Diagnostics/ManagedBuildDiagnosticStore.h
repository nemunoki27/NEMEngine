#pragma once

//============================================================================
//	include
//============================================================================
#include "ManagedBuildDiagnosticTypes.h"

// c++
#include <cstdint>
#include <deque>
#include <string>
#include <optional>

namespace Engine {

	//============================================================================
	//	ManagedBuildDiagnosticStore class
	//	Managedビルドの診断を上限付きで保持する
	//============================================================================
	class ManagedBuildDiagnosticStore {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 解析と共有する診断の上限
		static constexpr size_t kMaxEntries = ManagedBuildDiagnosticLimits::kMaxEntries;
		static constexpr size_t kMaxBuildHistory = ManagedBuildDiagnosticLimits::kMaxBuildHistory;
		static constexpr size_t kMaxMessageLength = ManagedBuildDiagnosticLimits::kMaxMessageLength;
		static constexpr size_t kMaxRawLength = ManagedBuildDiagnosticLimits::kMaxRawLength;
		static constexpr size_t kMaxPathLength = ManagedBuildDiagnosticLimits::kMaxPathLength;

		// 出力行を解析し診断以外は未取得を返す
		static std::optional<ManagedBuildDiagnostic> ParseLine(const std::string& rawLine);

		// ビルド開始を記録し古い履歴を間引く
		void BeginBuild(uint64_t buildID);

		// 出力行を解析し診断を追加できたか返す
		bool Ingest(uint64_t buildID, uint64_t reloadID, ManagedBuildProcessKind kind, const std::string& rawLine);

		// すべての診断を破棄する
		void Clear();

		//--------- accessor -----------------------------------------------------

		// 表示の更新判定に使う世代
		uint64_t Version() const { return version_; }

		// 診断を古い順で返し要素の参照は次の更新まで有効
		const std::deque<ManagedBuildDiagnostic>& Entries() const { return entries_; }

		// 重大度別の件数
		size_t ErrorCount() const { return errorCount_; }
		size_t WarningCount() const { return warningCount_; }

		// シングルトン
		static ManagedBuildDiagnosticStore& GetInstance();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 表示対象の診断
		std::deque<ManagedBuildDiagnostic> entries_;
		std::deque<uint64_t> buildHistory_; // 受け取った順のビルドID
		// 保持中の重大度別件数
		size_t errorCount_ = 0;
		size_t warningCount_ = 0;
		// 表示内容の更新世代
		uint64_t version_ = 0;

		//--------- functions ----------------------------------------------------

		// 上限超過分を間引く
		void EnforceBounds();
		// 重大度別の件数を数え直す
		void RecountSeverities();
	};
} // Engine
