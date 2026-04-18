#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Utility/Command/ICommand.h>

// c++
#include <memory>
#include <vector>

namespace Engine {

	//============================================================================
	//	CommandHistory class
	//	Undo / Redoを管理するコマンド履歴クラス
	//============================================================================
	template <typename T>
	class CommandHistory {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		CommandHistory() = default;
		~CommandHistory() = default;

		// コマンドを実行
		bool Execute(std::unique_ptr<ICommand<T>> command, T& context);

		// Undo / Redoを実行
		bool Undo(T& context);
		bool Redo(T& context);

		// 履歴を全てクリア
		void Clear();

		//--------- accessor -----------------------------------------------------

		bool CanUndo() const { return !undoStack_.empty(); }
		bool CanRedo() const { return !redoStack_.empty(); }

		size_t GetUndoCount() const { return undoStack_.size(); }
		size_t GetRedoCount() const { return redoStack_.size(); }

		const ICommand<T>* PeekUndo() const { return undoStack_.empty() ? nullptr : undoStack_.back().get(); }
		const ICommand<T>* PeekRedo() const { return redoStack_.empty() ? nullptr : redoStack_.back().get(); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// Undo/Redoの履歴スタック
		std::vector<std::unique_ptr<ICommand<T>>> undoStack_;
		std::vector<std::unique_ptr<ICommand<T>>> redoStack_;
	};

	//============================================================================
	//	CommandHistory classMethods
	//============================================================================

	template<typename T>
	inline bool CommandHistory<T>::Execute(std::unique_ptr<ICommand<T>> command, T& context) {

		if (!command) {
			return false;
		}

		// 実行に失敗したコマンドは履歴に積まない
		if (!command->Execute(context)) {
			return false;
		}

		// 成功したコマンドは履歴に積む
		undoStack_.emplace_back(std::move(command));
		redoStack_.clear();
		return true;
	}

	template<typename T>
	inline bool CommandHistory<T>::Undo(T& context) {

		// Undoスタックが空なら何もしない
		if (undoStack_.empty()) {
			return false;
		}

		// Undoスタックからコマンドを取り出して実行
		std::unique_ptr<ICommand<T>> command = std::move(undoStack_.back());
		undoStack_.pop_back();

		// コマンドのUndoを実行
		command->Undo(context);
		redoStack_.emplace_back(std::move(command));
		return true;
	}
	template<typename T>
	inline bool CommandHistory<T>::Redo(T& context) {

		// Redoスタックが空なら何もしない
		if (redoStack_.empty()) {
			return false;
		}

		// Redoスタックからコマンドを取り出して実行
		std::unique_ptr<ICommand<T>> command = std::move(redoStack_.back());
		redoStack_.pop_back();

		// コマンドのRedoを実行
		if (!command->Redo(context)) {
			// Redoに失敗したコマンドはRedoスタックに戻す
			redoStack_.emplace_back(std::move(command));
			return false;
		}

		// Redoに成功したコマンドはUndoスタックに積む
		undoStack_.emplace_back(std::move(command));
		return true;
	}

	template<typename T>
	inline void CommandHistory<T>::Clear() {

		undoStack_.clear();
		redoStack_.clear();
	}
} // Engine