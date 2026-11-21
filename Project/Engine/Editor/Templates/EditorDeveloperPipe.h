#pragma once

//============================================================================
//	include
//============================================================================
#include <chrono>
#include <optional>
#include <string>

// windows
#include <windows.h>

namespace Engine {

	//============================================================================
	//	EditorDeveloperPipe class
	//	開発者ランチャーからの要求をEditorの更新時に受信する
	//============================================================================
	class EditorDeveloperPipe {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		EditorDeveloperPipe() = default;
		~EditorDeveloperPipe();
		EditorDeveloperPipe(const EditorDeveloperPipe&) = delete;
		EditorDeveloperPipe& operator=(const EditorDeveloperPipe&) = delete;

		// 現在のEditor専用のパイプを作成する
		bool Open();
		// 受信が完了した要求を取り出す
		std::optional<std::string> Poll(bool accept);
		// 要求元へ結果を返す
		void Reply(const std::string& json);

		//--------- accessor -----------------------------------------------------

		const std::wstring& GetName() const { return name_; }
		bool HasRequest() const { return state_ == State::Processing; }

	private:
		enum class State { Waiting, Reading, Processing, Replied };

		HANDLE pipe_ = INVALID_HANDLE_VALUE;
		State state_ = State::Waiting;
		std::wstring name_;
		std::string buffer_;
		std::chrono::steady_clock::time_point connectedAt_;

		// 現在の接続を閉じて次の要求を受け付ける
		void Disconnect();
	};
} // Engine
