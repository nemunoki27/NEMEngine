#include "Logger.h"
#include <Engine/Utility/Algorithm/Algorithm.h>
#include <fstream>
#if defined(_MSC_VER)
#include <spdlog/sinks/base_sink.h>
#include <windows.h>
#endif

using namespace Engine;

namespace {
	constexpr std::array<std::string_view, static_cast<std::size_t>(LogType::Count)> kLoggerNames{
		"engine",
		"game"
	};
	constexpr std::array<std::string_view, static_cast<std::size_t>(LogType::Count)> kFileNames{
		"engine.log",
		"gameLogic.log"
	};
#if defined(_MSC_VER)
	class Utf8DebugSink final :
		public spdlog::sinks::base_sink<std::mutex> {
	protected:
		void sink_it_(const spdlog::details::log_msg& msg) override {

			// Visual Studioの出力ウィンドウへUTF-16で渡し、日本語ログの文字化けを避ける
			spdlog::memory_buf_t formatted;
			formatter_->format(msg, formatted);

			std::string text(formatted.data(), formatted.size());
			std::wstring wide = Algorithm::ConvertString(text);
			if (!wide.empty()) {
				::OutputDebugStringW(wide.c_str());
			}
		}

		void flush_() override {
		}
	};

	bool HasDebugSink(const std::shared_ptr<spdlog::logger>& logger) {
		if (!logger) return false;

		for (const auto& sink : logger->sinks()) {
			if (dynamic_cast<Utf8DebugSink*>(sink.get()) != nullptr) {
				return true;
			}
		}
		return false;
	}
#endif
}

void Logger::CreateLogFiles(const std::filesystem::path& logDir, bool truncate) {

	std::scoped_lock lock(mutex_);
	if (initialized_) return;

#if defined(_MSC_VER)
	// Windowsコンソール側もUTF-8に揃え、stdoutログの文字化けを避ける
	::SetConsoleOutputCP(CP_UTF8);
	::SetConsoleCP(CP_UTF8);
#endif

	logDir_ = logDir;
	if (logDir_.empty()) logDir_ = "./Log";

	std::error_code ec;
	std::filesystem::create_directories(logDir_, ec);

	loggers_.clear();
	loggers_.resize(static_cast<std::size_t>(LogType::Count));
	for (auto& logs : recentLogs_) {
		logs.clear();
	}

	for (std::size_t i = 0; i < loggers_.size(); ++i) {
		const auto type = static_cast<LogType>(i);
		const auto loggerName = std::string(TypeToLoggerName(type));
		const auto filePath = (logDir_ / std::string(TypeToFileName(type))).string();

		spdlog::drop(loggerName);

		try {
			std::vector<spdlog::sink_ptr> sinks;

			// 例: Engine だけ console + msvc、GameLogic はファイルのみ
			const bool withConsole = (type == LogType::Engine);

			if (withConsole) {
				sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
			}

			sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(filePath, truncate));

#if defined(_MSC_VER)
			if (withConsole) {
				sinks.push_back(std::make_shared<Utf8DebugSink>());
			}
#endif

			auto lg = std::make_shared<spdlog::logger>(loggerName, sinks.begin(), sinks.end());
			lg->set_level(spdlog::level::trace);
			lg->flush_on(spdlog::level::err);
			lg->set_pattern("[%H:%M:%S.%e] [%n] %v");

			spdlog::register_logger(lg);
			loggers_[i] = std::move(lg);
		}
		catch (const spdlog::spdlog_ex&) {
			loggers_[i].reset();
		}
	}

	if (loggers_[static_cast<std::size_t>(LogType::Engine)]) {
		spdlog::set_default_logger(loggers_[static_cast<std::size_t>(LogType::Engine)]);
	}

	initialized_ = true;
}

void Logger::Finalize() {

	std::scoped_lock lock(mutex_);
	if (!initialized_) return;

	for (std::size_t i = 0; i < loggers_.size(); ++i) {
		if (loggers_[i]) loggers_[i]->flush();
		spdlog::drop(std::string(TypeToLoggerName(static_cast<LogType>(i))));
	}

	loggers_.clear();
	for (auto& logs : recentLogs_) {
		logs.clear();
	}
	initialized_ = false;
}

void Logger::EnsureInitialized() {
	if (initialized_) return;
	CreateLogFiles(); // default
}

std::string_view Logger::TypeToFileName(LogType type) {
	return kFileNames[static_cast<std::size_t>(type)];
}
std::string_view Logger::TypeToLoggerName(LogType type) {
	return kLoggerNames[static_cast<std::size_t>(type)];
}

void Logger::Output(LogType type, std::string_view message, spdlog::level::level_enum level) {
	EnsureInitialized();
	auto& lg = Get(type);
	if (!lg) return;
	AppendRecentLog(type, level, std::string(message));
	lg->log(level, "{}", message);
}

void Logger::BlankLine(LogType type, std::uint32_t lines) {
	EnsureInitialized();

	std::scoped_lock lock(mutex_);

	// ファイル側へ「何も付かない空行」を直接追記
	const auto filePath = (logDir_ / std::string(TypeToFileName(type))).string();
	std::ofstream ofs(filePath, std::ios::out | std::ios::app);
	if (ofs.is_open()) {
		for (std::uint32_t i = 0; i < lines; ++i) {
			ofs << '\n';
		}
		ofs.flush();
	}

#if defined(_MSC_VER)
	// Visual Studio 出力ウィンドウにも空行を出す
	auto& lg = Get(type);
	if (HasDebugSink(lg)) {
		for (std::uint32_t i = 0; i < lines; ++i) {
			::OutputDebugStringW(L"\r\n");
		}
	}
#endif
}

void Engine::Logger::DrawLine(LogType type, std::uint32_t length, char ch) {

	EnsureInitialized();
	auto& lg = Get(type);
	if (!lg) return;
	lg->log(spdlog::level::info, std::string(length, ch));
}

void Engine::Logger::BeginSection(LogType type) {

	DrawLine(type);
}

void Engine::Logger::EndSection(LogType type) {

	DrawLine(type);
}

void Logger::Flush(LogType type) {
	EnsureInitialized();
	auto& lg = Get(type);
	if (!lg) return;
	lg->flush();
}

void Logger::FlushAll() {
	EnsureInitialized();
	for (auto& lg : loggers_) if (lg) lg->flush();
}

std::vector<Logger::LogEntry> Logger::GetRecentLogs(LogType type) {

	std::scoped_lock lock(mutex_);
	const auto index = static_cast<std::size_t>(type);
	return std::vector<LogEntry>(recentLogs_[index].begin(), recentLogs_[index].end());
}

void Logger::AppendRecentLog(LogType type, spdlog::level::level_enum level, std::string&& message) {

	std::scoped_lock lock(mutex_);
	const auto index = static_cast<std::size_t>(type);
	auto& logs = recentLogs_[index];
	logs.push_back(LogEntry{ level, std::move(message) });

	while (kRecentLogLimit_ < logs.size()) {
		logs.pop_front();
	}
}

//==================== ScopedOutput ====================

Logger::ScopedOutput::ScopedOutput(LogType type, std::string label)
	: type_(type), label_(std::move(label)), start_(Clock::now()) {
}

Logger::ScopedOutput::~ScopedOutput() noexcept {
	Finish();
}

void Logger::ScopedOutput::Finish() noexcept {
	if (!active_) return;
	active_ = false;

	try {
		using namespace std::chrono;
		const auto us = duration_cast<microseconds>(Clock::now() - start_).count();
		const double ms = static_cast<double>(us) / 1000.0;
		Logger::Output(type_, spdlog::level::info, "[TIMER] {} : {:.3f} ms", label_, ms);
	}
	catch (...) {
	}
}

Logger::ScopedOutput::ScopedOutput(ScopedOutput&& other) noexcept
	: type_(other.type_),
	label_(std::move(other.label_)),
	start_(other.start_),
	active_(other.active_) {
	other.active_ = false;
}

Logger::ScopedOutput& Logger::ScopedOutput::operator=(ScopedOutput&& other) noexcept {
	if (this == &other) return *this;
	Finish();
	type_ = other.type_;
	label_ = std::move(other.label_);
	start_ = other.start_;
	active_ = other.active_;
	other.active_ = false;
	return *this;
}
