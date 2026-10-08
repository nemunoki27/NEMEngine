#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstddef>
#include <cstdint>
#include <string>

namespace Engine {

	//============================================================================
	//	DiagnosticSeverity enum
	//	ビルド処理から抽出した診断の重大度
	//============================================================================
	enum class DiagnosticSeverity : uint8_t {

		Info,
		Warning,
		Error,
	};

	//============================================================================
	//	ManagedBuildProcessKind enum
	//	診断を受け取った処理の種類
	//============================================================================
	enum class ManagedBuildProcessKind : uint8_t {

		MetadataSync,
		Build,
		ProjectRefresh,
		IDELaunch,
		Other,
	};

	//============================================================================
	//	ManagedBuildDiagnostic struct
	//	構造化した診断1件
	//============================================================================
	struct ManagedBuildDiagnostic {

		uint64_t buildID = 0; // ビルド番号
		uint64_t reloadID = 0; // 再読込番号
		ManagedBuildProcessKind processKind = ManagedBuildProcessKind::Other; // 診断元の工程
		DiagnosticSeverity severity = DiagnosticSeverity::Info; // 重大度
		std::string code;     // 診断コード
		std::string message; // 診断本文
		std::string file;     // 診断元のファイル名
		int32_t line = 0; // 未取得は0
		int32_t column = 0; // 未取得は0
		std::string rawLine; // 元の出力行
		std::string timestamp; // HH:MM:SS
	};

	// 解析と履歴表示で共有する診断の上限
	namespace ManagedBuildDiagnosticLimits {

		inline constexpr size_t kMaxEntries = 2000;       // 保持する診断数の上限
		inline constexpr size_t kMaxBuildHistory = 16;    // 保持するビルド履歴数の上限
		inline constexpr size_t kMaxMessageLength = 1024; // 本文の最大長
		inline constexpr size_t kMaxRawLength = 2048;     // 元の出力行の最大長
		inline constexpr size_t kMaxPathLength = 512;     // ファイル名の最大長
	}
}
