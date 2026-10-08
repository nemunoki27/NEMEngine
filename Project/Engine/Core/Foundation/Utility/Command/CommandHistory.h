#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Command/ICommand.h>
#include <Engine/Core/Foundation/Utility/ScopedValue.h>

// c++
#include <memory>
#include <stdexcept>
#include <vector>

namespace Engine {

	//============================================================================
	//	CommandHistory class
	//	実行と取消と再実行の履歴を所有する
	//============================================================================
	template <typename T>
	class CommandHistory {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		CommandHistory() = default;
		~CommandHistory() = default;

		// コマンドを実行
		bool Execute(std::unique_ptr<ICommand<T>> command, T& context);

		// 直前の操作を取り消す
		bool Undo(T& context);
		// 取り消した操作を再実行する
		bool Redo(T& context);

		// 履歴を全てクリア
		void Clear();

		//--------- accessor -----------------------------------------------------

		// 取消可能な履歴があるか確認する
		bool CanUndo() const { return !undoStack_.empty(); }
		// 再実行可能な履歴があるか確認する
		bool CanRedo() const { return !redoStack_.empty(); }

		// 取消履歴の件数を取得する
		size_t GetUndoCount() const { return undoStack_.size(); }
		// 再実行履歴の件数を取得する
		size_t GetRedoCount() const { return redoStack_.size(); }

		// 直前の取消対象を借用する
		const ICommand<T>* PeekUndo() const { return undoStack_.empty() ? nullptr : undoStack_.back().get(); }
		// 直前の再実行対象を借用する
		const ICommand<T>* PeekRedo() const { return redoStack_.empty() ? nullptr : redoStack_.back().get(); }

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// 取消と再実行の履歴
		std::vector<std::unique_ptr<ICommand<T>>> undoStack_;
		std::vector<std::unique_ptr<ICommand<T>>> redoStack_;
		bool executing_ = false; // 再入を拒否する実行状態

		//--------- functions ----------------------------------------------------

		// World変更前に履歴の格納先を確保する
		static void PreparePush(std::vector<std::unique_ptr<ICommand<T>>>& stack);
	};

	//============================================================================
	//	CommandHistory classTemplateMethods
	//============================================================================
	template <typename T>
	inline bool CommandHistory<T>::Execute(std::unique_ptr<ICommand<T>> command, T& context) {

		if (!command || executing_) {
			return false;
		}
		ScopedValue operation(executing_, true);

		// 連続操作を最初の取消基準でまとめる
		if (!undoStack_.empty() && undoStack_.back()->CanCoalesce(*command)) {

			if (!undoStack_.back()->ExecuteCoalesced(*command, context)) {
				return false;
			}
			redoStack_.clear();
			return true;
		}

		// 実行に失敗したコマンドは履歴に積まない
		PreparePush(undoStack_);
		if (!command->Execute(context)) {
			return false;
		}

		// 成功したコマンドは履歴に積む
		undoStack_.emplace_back(std::move(command));
		redoStack_.clear();
		return true;
	}

	template <typename T>
	inline bool CommandHistory<T>::Undo(T& context) {

		// 取消履歴と実行状態を確認
		if (undoStack_.empty() || executing_) {
			return false;
		}

		ScopedValue operation(executing_, true);
		PreparePush(redoStack_);

		// 例外時は元の履歴を残し、成功後だけ移す
		undoStack_.back()->Undo(context);
		redoStack_.emplace_back(std::move(undoStack_.back()));
		undoStack_.pop_back();
		return true;
	}
	template <typename T>
	inline bool CommandHistory<T>::Redo(T& context) {

		// 再実行履歴と実行状態を確認
		if (redoStack_.empty() || executing_) {
			return false;
		}

		ScopedValue operation(executing_, true);
		PreparePush(undoStack_);

		// 対象コマンドを再実行
		if (!redoStack_.back()->Redo(context)) {
			return false;
		}

		// 成功した履歴を取消側へ移す
		undoStack_.emplace_back(std::move(redoStack_.back()));
		redoStack_.pop_back();
		return true;
	}

	template <typename T>
	inline void CommandHistory<T>::Clear() {

		if (executing_) {
			throw std::logic_error("Command実行中に履歴を破棄できません");
		}
		undoStack_.clear();
		redoStack_.clear();
	}

	template <typename T>
	inline void CommandHistory<T>::PreparePush(std::vector<std::unique_ptr<ICommand<T>>>& stack) {

		if (stack.size() < stack.capacity()) {
			return;
		}
		if (stack.size() == stack.max_size()) {
			throw std::length_error("Command履歴の上限に達しました");
		}
		// 毎回の再確保を避けて容量を増やす
		const size_t capacity = stack.empty() ? 1 : stack.size();
		stack.reserve(capacity > stack.max_size() / 2 ? stack.max_size() : capacity * 2);
	}
}
