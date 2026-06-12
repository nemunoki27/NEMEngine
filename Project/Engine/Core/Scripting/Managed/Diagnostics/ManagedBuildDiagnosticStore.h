#pragma once

//============================================================================
//	include
//============================================================================
#include <cstdint>
#include <deque>
#include <string>
#include <optional>

namespace Engine {

	//============================================================================
	//	DiagnosticSeverity enum
	//	buildやmetadata syncやproject refreshの出力から構造化した診断の重大度
	//============================================================================
	enum class DiagnosticSeverity : uint8_t {

		Info,
		Warning,
		Error,
	};

	//============================================================================
	//	ManagedBuildProcessKind enum
	//	どの工程の出力か、Console log再解析ではなくingestion時に分類する
	//============================================================================
	enum class ManagedBuildProcessKind : uint8_t {

		MetadataSync,
		Build,
		ProjectRefresh,
		IdeLaunch,
		Other,
	};

	//============================================================================
	//	ManagedBuildDiagnostic struct
	//	構造化した診断1件
	//============================================================================
	struct ManagedBuildDiagnostic {

		uint64_t buildId = 0;
		uint64_t reloadId = 0;
		ManagedBuildProcessKind processKind = ManagedBuildProcessKind::Other;
		DiagnosticSeverity severity = DiagnosticSeverity::Info;
		std::string code;     // CS1002 や MSB3021 等
		std::string message;
		std::string file;     // 絶対または相対 path、無ければ空
		int32_t line = 0;
		int32_t column = 0;
		std::string rawLine;
		std::string timestamp; // HH:MM:SS
	};

	//============================================================================
	//	ManagedBuildDiagnosticStore class
	//	MSBuildやmetadata sync出力をingestion点でparseして保持するbounded store
	//============================================================================
	class ManagedBuildDiagnosticStore {
	public:
		//========================================================================
		//	bounds
		//========================================================================

		static constexpr size_t kMaxEntries = 2000;       // 保持する診断 entry の総数上限
		static constexpr size_t kMaxBuildHistory = 16;    // 履歴として残す build 数の上限
		static constexpr size_t kMaxMessageLength = 1024; // message の最大長
		static constexpr size_t kMaxRawLength = 2048;     // rawLine の最大長
		static constexpr size_t kMaxPathLength = 512;     // file path の最大長

		//========================================================================
		//	public Methods
		//========================================================================

		// 新しいbuildサイクルの開始を記録する、古いbuildの履歴を上限で間引く
		void BeginBuild(uint64_t buildId);

		// 1行をingestionし診断としてparseできた場合のみstoreへ追加しtrueを返す、parse不能行は入れずConsole logは呼び出し側が残す
		bool Ingest(uint64_t buildId, uint64_t reloadId, ManagedBuildProcessKind kind, const std::string& rawLine);

		// 全entryを破棄する
		void Clear();

		//--------- accessor -----------------------------------------------------

		// 内容が変わるたびに増えるversion、UIが再構築要否を判断するのに使い巨大copyを避ける
		uint64_t Version() const { return version_; }

		// 保持中の診断を古い順で返す、main threadからの読み取り専用参照
		const std::deque<ManagedBuildDiagnostic>& Entries() const { return entries_; }

		// severity別件数
		size_t ErrorCount() const { return errorCount_; }
		size_t WarningCount() const { return warningCount_; }

		// MSBuild行をparseする、静的で副作用なくtestから直接呼べ診断行でなければnullopt
		static std::optional<ManagedBuildDiagnostic> ParseLine(const std::string& rawLine);

		// シングルトン
		static ManagedBuildDiagnosticStore& GetInstance();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// 上限超過分を間引く
		void EnforceBounds();
		// severity別件数を数え直す
		void RecountSeverities();

		//--------- variables ----------------------------------------------------

		std::deque<ManagedBuildDiagnostic> entries_;
		std::deque<uint64_t> buildHistory_; // 受け入れ順の buildId
		size_t errorCount_ = 0;
		size_t warningCount_ = 0;
		uint64_t version_ = 0;
	};
} // Engine
