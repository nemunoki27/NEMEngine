#include "ManagedProcessRunner.h"

//============================================================================
//	include
//============================================================================
// windows
#include <windows.h>
// c++
#include <vector>

namespace {

	// 現在の環境ブロックを複製し、DOTNET_CLI_UI_LANGUAGE=en を上書き設定した
	// Unicode 環境ブロック（ダブルnull終端）を構築する。
	std::vector<wchar_t> BuildChildEnvironmentBlock() {

		std::vector<wchar_t> block;

		// 既存環境をコピーする（同名の DOTNET_CLI_UI_LANGUAGE は後で上書きするため除外）
		LPWCH environment = ::GetEnvironmentStringsW();
		if (environment) {

			const wchar_t* cursor = environment;
			while (*cursor) {

				const size_t length = std::wcslen(cursor);
				const std::wstring_view entry(cursor, length);

				// "NAME=VALUE" 形式。先頭が '=' のドライブカレントエントリは保持する
				const bool isUiLanguage =
					entry.size() >= 23 && _wcsnicmp(cursor, L"DOTNET_CLI_UI_LANGUAGE=", 23) == 0;
				if (!isUiLanguage) {

					block.insert(block.end(), cursor, cursor + length + 1); // 末尾nullも含めてコピー
				}
				cursor += length + 1;
			}
			::FreeEnvironmentStringsW(environment);
		}

		// dotnet CLI を英語に固定する（ログを安定させる）
		static const wchar_t kUiLanguage[] = L"DOTNET_CLI_UI_LANGUAGE=en";
		block.insert(block.end(), std::begin(kUiLanguage), std::end(kUiLanguage)); // 末尾nullを含む

		// 環境ブロックのダブルnull終端
		block.push_back(L'\0');
		return block;
	}
}

//============================================================================
//	ManagedProcessRunner classMethods
//============================================================================
Engine::ManagedProcessRunner::~ManagedProcessRunner() {
	Terminate();
}

bool Engine::ManagedProcessRunner::Start(const std::wstring& commandLine, const std::filesystem::path& workingDirectory) {

	// 多重起動はしない（呼び出し側が完了を待ってから再起動する）
	if (process_) {
		return false;
	}

	// stdout/stderr をまとめて受ける匿名 pipe を作る。write end のみ継承させる
	SECURITY_ATTRIBUTES security{};
	security.nLength = sizeof(security);
	security.bInheritHandle = TRUE;
	security.lpSecurityDescriptor = nullptr;

	HANDLE readEnd = nullptr;
	HANDLE writeEnd = nullptr;
	if (!::CreatePipe(&readEnd, &writeEnd, &security, 0)) {
		return false;
	}
	// read end は継承させない（子プロセスへ渡さない）
	::SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFOW startupInfo{};
	startupInfo.cb = sizeof(startupInfo);
	startupInfo.dwFlags = STARTF_USESTDHANDLES;
	startupInfo.hStdOutput = writeEnd;
	startupInfo.hStdError = writeEnd;
	startupInfo.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);

	// CreateProcessW はコマンドラインを書き換える可能性があるため、可変バッファへコピーする
	std::vector<wchar_t> commandBuffer(commandLine.begin(), commandLine.end());
	commandBuffer.push_back(L'\0');

	std::vector<wchar_t> environmentBlock = BuildChildEnvironmentBlock();
	const std::wstring workingDirectoryString = workingDirectory.empty() ? std::wstring{} : workingDirectory.wstring();

	PROCESS_INFORMATION processInfo{};
	const BOOL created = ::CreateProcessW(
		nullptr,
		commandBuffer.data(),
		nullptr,
		nullptr,
		TRUE, // pipe write end を継承させる
		CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
		environmentBlock.data(),
		workingDirectoryString.empty() ? nullptr : workingDirectoryString.c_str(),
		&startupInfo,
		&processInfo);

	// 親側では write end は不要（子だけが書く）。閉じることで子終了時に read 側へ EOF が伝わる
	::CloseHandle(writeEnd);

	if (!created) {

		::CloseHandle(readEnd);
		return false;
	}

	process_ = processInfo.hProcess;
	thread_ = processInfo.hThread;
	stdoutRead_ = readEnd;
	pending_.clear();
	exitCode_ = -1;
	return true;
}

bool Engine::ManagedProcessRunner::Poll(const std::function<void(const std::string&)>& onLine) {

	if (!process_) {
		return true;
	}

	// 取り込めるだけ取り込む（非ブロッキング）
	DrainPipe(onLine);

	// プロセス終了を確認する（待たない）
	if (::WaitForSingleObject(static_cast<HANDLE>(process_), 0) != WAIT_OBJECT_0) {
		return false;
	}

	// 終了確定。残出力を flush してから exit code を確定する
	DrainPipe(onLine);
	if (!pending_.empty()) {

		onLine(pending_);
		pending_.clear();
	}

	DWORD code = 0;
	if (::GetExitCodeProcess(static_cast<HANDLE>(process_), &code)) {
		exitCode_ = static_cast<int32_t>(code);
	}
	CloseHandles();
	return true;
}

void Engine::ManagedProcessRunner::Terminate() {

	if (process_) {

		// まだ実行中なら終了させてから回収する
		if (::WaitForSingleObject(static_cast<HANDLE>(process_), 0) != WAIT_OBJECT_0) {

			::TerminateProcess(static_cast<HANDLE>(process_), 1);
			::WaitForSingleObject(static_cast<HANDLE>(process_), 2000);
		}
	}
	CloseHandles();
	pending_.clear();
}

void Engine::ManagedProcessRunner::CloseHandles() {

	if (thread_) {
		::CloseHandle(static_cast<HANDLE>(thread_));
		thread_ = nullptr;
	}
	if (process_) {
		::CloseHandle(static_cast<HANDLE>(process_));
		process_ = nullptr;
	}
	if (stdoutRead_) {
		::CloseHandle(static_cast<HANDLE>(stdoutRead_));
		stdoutRead_ = nullptr;
	}
}

void Engine::ManagedProcessRunner::DrainPipe(const std::function<void(const std::string&)>& onLine) {

	if (!stdoutRead_) {
		return;
	}

	char buffer[4096];
	for (;;) {

		// まず読めるバイト数を確認する（ブロックしないため）
		DWORD available = 0;
		if (!::PeekNamedPipe(static_cast<HANDLE>(stdoutRead_), nullptr, 0, nullptr, &available, nullptr)) {
			// pipe が壊れた/閉じた
			break;
		}
		if (available == 0) {
			break;
		}

		DWORD read = 0;
		const DWORD toRead = available < sizeof(buffer) ? available : static_cast<DWORD>(sizeof(buffer));
		if (!::ReadFile(static_cast<HANDLE>(stdoutRead_), buffer, toRead, &read, nullptr) || read == 0) {
			break;
		}

		pending_.append(buffer, read);

		// 行単位で onLine へ流す
		size_t newlinePos = pending_.find('\n');
		while (newlinePos != std::string::npos) {

			std::string line = pending_.substr(0, newlinePos);
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			onLine(line);
			pending_.erase(0, newlinePos + 1);
			newlinePos = pending_.find('\n');
		}
	}
}
