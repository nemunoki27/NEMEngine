#pragma once

//============================================================================
//	include
//============================================================================
namespace Engine {

	//============================================================================
	//	ICommand class
	//	コマンドの共通インターフェース
	//============================================================================
	template <typename T>
	class ICommand {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ICommand() = default;
		virtual ~ICommand() = default;

		// 実行
		virtual bool Execute(T& context) = 0;

		// 処理の取り消し
		virtual void Undo(T& context) = 0;
		// 処理の復元
		virtual bool Redo(T& context) { return Execute(context); }

		// 直前コマンドへ連続操作をまとめられるか
		virtual bool CanCoalesce([[maybe_unused]] const ICommand<T>& next) const {
			return false;
		}
		// 直前コマンドのUndo基準を維持したまま次の操作を実行
		virtual bool ExecuteCoalesced([[maybe_unused]] ICommand<T>& next, [[maybe_unused]] T& context) {
			return false;
		}

		//--------- accessor -----------------------------------------------------

		// デバッグ表示用
		virtual const char* GetName() const = 0;
	};
} // Engine
