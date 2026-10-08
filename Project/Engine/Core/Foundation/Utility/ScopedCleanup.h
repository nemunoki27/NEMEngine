#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <type_traits>
#include <utility>

namespace Engine {

	//============================================================================
	//	ScopedCleanup class
	//	処理範囲を抜ける際に後処理を実行する
	//============================================================================
	template <typename T>
	class ScopedCleanup {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 後処理を保持する
		explicit ScopedCleanup(T action) noexcept;
		// 例外で抜けた場合も後処理を実行する
		~ScopedCleanup();
		ScopedCleanup(const ScopedCleanup&) = delete;
		ScopedCleanup& operator=(const ScopedCleanup&) = delete;

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 終了処理から例外を送出しない
		static_assert(
			std::is_nothrow_move_constructible_v<T> && std::is_nothrow_destructible_v<T> && std::is_nothrow_invocable_v<T&>);
		// 終了時に実行する処理
		T action_;
	};

	//============================================================================
	//	ScopedCleanup classTemplateMethods
	//============================================================================

	template <typename T>
	ScopedCleanup<T>::ScopedCleanup(T action) noexcept : action_(std::move(action)) {
	}

	template <typename T>
	ScopedCleanup<T>::~ScopedCleanup() {

		// 所有元の終了前に後処理を実行
		action_();
	}
}
