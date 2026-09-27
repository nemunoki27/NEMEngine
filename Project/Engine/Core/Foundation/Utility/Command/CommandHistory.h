#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Command/ICommand.h>

// c++
#include <memory>
#include <stdexcept>
#include <vector>

namespace Engine {

	//============================================================================
	//	CommandHistory class
	//	Undo / Redoを管理するコマンド履歴クラス
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
		//============================================================================
		//	private Methods
		//============================================================================

		// Callbackから同じ履歴を操作させない
		class OperationScope {
		public:
			explicit OperationScope(bool& active) : active_(active) { active_ = true; }
			~OperationScope() { active_ = false; }
			OperationScope(const OperationScope&) = delete;
			OperationScope& operator=(const OperationScope&) = delete;
		private:
			bool& active_;
		};

		// World変更前に履歴の格納先を確保する
		static void PreparePush(std::vector<std::unique_ptr<ICommand<T>>>& stack);

		//--------- variables ----------------------------------------------------

		// Undo/Redoの履歴スタック
		std::vector<std::unique_ptr<ICommand<T>>> undoStack_;
		std::vector<std::unique_ptr<ICommand<T>>> redoStack_;
		bool executing_ = false;
	};

	//============================================================================
	//	CommandHistory classMethods
	//============================================================================
	template<typename T>
	inline bool CommandHistory<T>::Execute(std::unique_ptr<ICommand<T>> command, T& context) {

		if (!command || executing_) {
			return false;
		}
		OperationScope operation(executing_);

		// 同じ対象への連続操作は直前のUndo基準を残して1件へまとめる
		if (!undoStack_.empty() &&
			undoStack_.back()->CanCoalesce(*command)) {

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

	template<typename T>
	inline bool CommandHistory<T>::Undo(T& context) {

		// Undoスタックが空なら何もしない
		if (undoStack_.empty() || executing_) {
			return false;
		}

		OperationScope operation(executing_);
		PreparePush(redoStack_);

		// 例外時は元の履歴を残し、成功後だけ移す
		undoStack_.back()->Undo(context);
		redoStack_.emplace_back(std::move(undoStack_.back()));
		undoStack_.pop_back();
		return true;
	}
	template<typename T>
	inline bool CommandHistory<T>::Redo(T& context) {

		// Redoスタックが空なら何もしない
		if (redoStack_.empty() || executing_) {
			return false;
		}

		OperationScope operation(executing_);
		PreparePush(undoStack_);

		// コマンドのRedoを実行
		if (!redoStack_.back()->Redo(context)) {
			return false;
		}

		// Redoに成功したコマンドはUndoスタックに積む
		undoStack_.emplace_back(std::move(redoStack_.back()));
		redoStack_.pop_back();
		return true;
	}

	template<typename T>
	inline void CommandHistory<T>::Clear() {

		if (executing_) {
			throw std::logic_error("Command実行中に履歴を破棄できません");
		}
		undoStack_.clear();
		redoStack_.clear();
	}

	template<typename T>
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
} // Engine

