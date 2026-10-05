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
	template<typename T>
	class SnapshotCommand : public ICommand<T> {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SnapshotCommand(T before, T after, std::string name) :
			before_(std::move(before)), after_(std::move(after)), name_(std::move(name)) {

			static_assert(std::is_nothrow_move_assignable_v<T>);
		}
		bool Execute(T& context) override {

			// コピーの成功後だけ編集値を差し替える
			T replacement = after_;
			context = std::move(replacement);
			return true;
		}
		void Undo(T& context) override {

			T replacement = before_;
			context = std::move(replacement);
		}

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return name_.c_str(); }
	private:
		//--------- variables ----------------------------------------------------

		T before_;
		T after_;
		std::string name_;
	};
}
