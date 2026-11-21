#include "EditorDeveloperPipe.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Identity/AssetGUID.h>
#include <Engine/Core/Foundation/Utility/Algorithm/UTFConversion.h>

// c++
#include <vector>
// windows
#include <sddl.h>

#pragma comment(lib, "Advapi32.lib")

Engine::EditorDeveloperPipe::~EditorDeveloperPipe() {

	if (pipe_ != INVALID_HANDLE_VALUE) ::CloseHandle(pipe_);
}

bool Engine::EditorDeveloperPipe::Open() {

	if (pipe_ != INVALID_HANDLE_VALUE) return true;
	name_ = L"NEMDeveloperEditor-" + std::to_wstring(::GetCurrentProcessId()) + L"-" +
		Algorithm::ConvertString(ToString(AssetGUID::New()));
	const auto path = L"\\\\.\\pipe\\" + name_;
	HANDLE token = nullptr;
	if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
	DWORD bytes = 0;
	::GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
	std::vector<BYTE> user(bytes);
	const BOOL identified = ::GetTokenInformation(token, TokenUser, user.data(), bytes, &bytes);
	::CloseHandle(token);
	LPWSTR sid = nullptr;
	if (!identified || !::ConvertSidToStringSidW(reinterpret_cast<const TOKEN_USER*>(user.data())->User.Sid, &sid)) return false;
	const std::wstring access = L"O:" + std::wstring(sid) + L"D:P(A;;GA;;;" + sid + L")";
	::LocalFree(sid);
	PSECURITY_DESCRIPTOR descriptor = nullptr;
	if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(access.c_str(), SDDL_REVISION_1,
		&descriptor, nullptr)) return false;
	SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
	pipe_ = ::CreateNamedPipeW(path.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 32768, 32768, 1000, &security);
	::LocalFree(descriptor);
	return pipe_ != INVALID_HANDLE_VALUE;
}

void Engine::EditorDeveloperPipe::Disconnect() {

	::DisconnectNamedPipe(pipe_);
	state_ = State::Waiting;
	buffer_.clear();
}

std::optional<std::string> Engine::EditorDeveloperPipe::Poll(bool accept) {

	if (pipe_ == INVALID_HANDLE_VALUE) return {};
	if (state_ == State::Waiting) {
		if (!accept) return {};
		if (!::ConnectNamedPipe(pipe_, nullptr)) {
			const DWORD error = ::GetLastError();
			if (error == ERROR_NO_DATA) Disconnect();
			if (error != ERROR_PIPE_CONNECTED) return {};
		}
		connectedAt_ = std::chrono::steady_clock::now();
		state_ = State::Reading;
	}
	DWORD available = 0;
	if (!::PeekNamedPipe(pipe_, nullptr, 0, nullptr, &available, nullptr)) {
		Disconnect();
		return {};
	}
	if (state_ != State::Processing && std::chrono::steady_clock::now() - connectedAt_ > std::chrono::seconds(30)) {
		Disconnect();
		return {};
	}
	if (state_ != State::Reading || available == 0) return {};
	if (buffer_.size() + available > 256 * 1024) {
		Disconnect();
		return {};
	}
	const size_t offset = buffer_.size();
	buffer_.resize(offset + available);
	DWORD count = 0;
	if (!::ReadFile(pipe_, buffer_.data() + offset, available, &count, nullptr)) {
		Disconnect();
		return {};
	}
	buffer_.resize(offset + count);
	const size_t end = buffer_.find('\n');
	if (end == std::string::npos) return {};
	buffer_.resize(end);
	state_ = State::Processing;
	return std::move(buffer_);
}

void Engine::EditorDeveloperPipe::Reply(const std::string& json) {

	if (!HasRequest()) return;
	const std::string message = json + "\n";
	DWORD written = 0;
	if (!::WriteFile(pipe_, message.data(), static_cast<DWORD>(message.size()), &written, nullptr) || written != message.size()) {
		Disconnect();
		return;
	}
	state_ = State::Replied;
	connectedAt_ = std::chrono::steady_clock::now();
}
