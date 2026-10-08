#pragma once

//============================================================================
//	include
//============================================================================
#include "ICommand.h"

// c++
#include <string>
#include <type_traits>
#include <utility>

namespace Engine {

	//============================================================================
	//	SnapshotCommand class
	//	編集前後の値を保持して既存履歴へ接続する
	//============================================================================
	template <typename T>
	class SnapshotCommand : public ICommand<T> {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 編集前後の値を保持する
		SnapshotCommand(T before, T after, std::string name);

		// 編集後の値を適用する
		bool Execute(T& context) override;
		// 編集前の値へ戻す
		void Undo(T& context) override;

		//--------- accessor -----------------------------------------------------

		// 履歴に表示する名前を取得する
		const char* GetName() const override { return name_.c_str(); }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		T before_;		   // 編集前の値
		T after_;		   // 編集後の値
		std::string name_; // 履歴の表示名
	};

	//============================================================================
	//	SnapshotCommand classTemplateMethods
	//============================================================================

	template <typename T>
	inline SnapshotCommand<T>::SnapshotCommand(T before, T after, std::string name)
		: before_(std::move(before)), after_(std::move(after)), name_(std::move(name)) {

		static_assert(std::is_nothrow_move_assignable_v<T>);
	}

	template <typename T>
	inline bool SnapshotCommand<T>::Execute(T& context) {

		// コピーの成功後だけ編集値を差し替える
		T replacement = after_;
		context = std::move(replacement);
		return true;
	}

	template <typename T>
	inline void SnapshotCommand<T>::Undo(T& context) {

		// コピーの成功後だけ編集前の値へ戻す
		T replacement = before_;
		context = std::move(replacement);
	}

}
