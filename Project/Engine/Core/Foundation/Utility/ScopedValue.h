#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <type_traits>

namespace Engine {

	//============================================================================
	//	ScopedValue class
	//	処理範囲内の値変更を終了時に元へ戻す
	//============================================================================
	template <typename T>
	class ScopedValue {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 現在値を保持して一時値を設定する
		ScopedValue(T& target, std::type_identity_t<T> value) noexcept;
		// 例外で抜けた場合も現在値を戻す
		~ScopedValue();
		ScopedValue(const ScopedValue&) = delete;
		ScopedValue& operator=(const ScopedValue&) = delete;

	private:
		//--------- variables ----------------------------------------------------

		// 復元時に例外を発生させない型だけを扱う
		static_assert(std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_copy_assignable_v<T>);

		// 一時変更する値
		T& target_;
		// 変更前の値
		T previous_;
	};

	//============================================================================
	//	ScopedValue classTemplateMethods
	//============================================================================

	template <typename T>
	ScopedValue<T>::ScopedValue(T& target, std::type_identity_t<T> value) noexcept : target_(target), previous_(target) {

		// 復元する値を保持してから変更する
		target_ = value;
	}

	template <typename T>
	ScopedValue<T>::~ScopedValue() {

		// 呼出し元の値へ戻す
		target_ = previous_;
	}
}
