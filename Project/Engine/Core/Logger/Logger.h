#pragma once

//============================================================================
//	include
//============================================================================

// spdlog
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#if defined(_MSC_VER)
#include <spdlog/sinks/msvc_sink.h>
#endif
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/fmt/fmt.h>

// c++
#include <filesystem>
#include <cstdint>
#include <mutex>
#include <array>
#include <vector>
#include <deque>
#include <chrono>
#include <string>
#include <string_view>

namespace Engine {

	// ログの種類
	enum class LogType : std::uint32_t {
		Engine = 0,
		GameLogic,
		Count
	};

	//============================================================================
	//	Logger class
	//============================================================================
	class Logger {
	public:

		struct LogEntry {
			spdlog::level::level_enum level = spdlog::level::info;
			std::string message;
		};

		//========================================================================
		//	public Methods
		//========================================================================

		Logger() = default;
		~Logger() = default;

		// ログ出力対象ファイルの作成
		static void CreateLogFiles(const std::filesystem::path& logDir = "./Log", bool truncate = true);
		static void Finalize();

		// 既に整形済み文字列を出力
		static void Output(LogType type, std::string_view message,
			spdlog::level::level_enum level = spdlog::level::info);

		// fmt形式で型安全に出力
		template <typename... Args>
		static void Output(LogType type, spdlog::level::level_enum level,
			fmt::format_string<Args...> fmtStr, Args&&... args);
		template <typename... Args>
		static void Output(LogType type, fmt::format_string<Args...> fmtStr, Args&&... args);

		// 空行を入れる
		static void BlankLine(LogType type, std::uint32_t lines = 1);
		// ラインを引く
		static void DrawLine(LogType type, std::uint32_t length = 128, char ch = '=');

		// ログを見やすくするためのヘルパー
		static void BeginSection(LogType type);
		static void EndSection(LogType type);

		static void Flush(LogType type);
		static void FlushAll();
		static std::vector<LogEntry> GetRecentLogs(LogType type);

		// スコープ滞在時間を自動記録
		class ScopedOutput final {
		public:

			ScopedOutput(LogType type, std::string label);

			template <typename... Args>
			ScopedOutput(LogType type, fmt::format_string<Args...> fmtStr, Args&&... args)
				: ScopedOutput(type, fmt::format(fmtStr, std::forward<Args>(args)...)) {
			}

			~ScopedOutput() noexcept;

			ScopedOutput(const ScopedOutput&) = delete;
			ScopedOutput& operator=(const ScopedOutput&) = delete;
			ScopedOutput(ScopedOutput&& other) noexcept;
			ScopedOutput& operator=(ScopedOutput&& other) noexcept;

		private:
			using Clock = std::chrono::steady_clock;
			void Finish() noexcept;

			LogType type_{};
			std::string label_;
			Clock::time_point start_{};
			bool active_{ true };
		};

		template <typename... Args>
		static ScopedOutput Scoped(LogType type, fmt::format_string<Args...> fmtStr, Args&&... args) {
			return ScopedOutput(type, fmtStr, std::forward<Args>(args)...);
		}
		static ScopedOutput Scoped(LogType type, std::string label) {
			return ScopedOutput(type, std::move(label));
		}

		// accessor
		static std::shared_ptr<spdlog::logger>& Get(LogType type) {
			return loggers_[static_cast<std::size_t>(type)];
		}
		static const std::filesystem::path& GetLogDir() { return logDir_; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		static void EnsureInitialized();
		static std::string_view TypeToFileName(LogType type);
		static std::string_view TypeToLoggerName(LogType type);
		static void AppendRecentLog(LogType type, spdlog::level::level_enum level, std::string&& message);

		static inline std::vector<std::shared_ptr<spdlog::logger>> loggers_;
		static inline std::filesystem::path logDir_ = "./Log";
		static inline std::mutex mutex_;
		static inline bool initialized_{ false };
		static constexpr std::size_t kRecentLogLimit_ = 30;
		static inline std::array<std::deque<LogEntry>, static_cast<std::size_t>(LogType::Count)> recentLogs_;
	};

	template <typename... Args>
	inline void Logger::Output(LogType type,
		spdlog::level::level_enum level,
		fmt::format_string<Args...> fmtStr,
		Args&&... args) {

		EnsureInitialized();
		auto& lg = Get(type);
		if (!lg) return;

		std::string text = fmt::format(fmtStr, std::forward<Args>(args)...);
		AppendRecentLog(type, level, std::string(text));
		lg->log(level, "{}", text);
	}

	template <typename... Args>
	inline void Logger::Output(LogType type,
		fmt::format_string<Args...> fmtStr,
		Args&&... args) {

		Output(type, spdlog::level::info, fmtStr, std::forward<Args>(args)...);
	}
}
