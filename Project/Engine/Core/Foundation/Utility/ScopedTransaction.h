#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <type_traits>
#include <utility>

namespace Engine {

	//============================================================================
	//	ScopedTransaction class
	//	未確定の変更を処理終了時に元へ戻す
	//============================================================================
	template <typename T>
	class ScopedTransaction {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 変更前の値を取得する
		explicit ScopedTransaction(T& target);
		// 未確定の変更を戻す
		~ScopedTransaction();
		ScopedTransaction(const ScopedTransaction&) = delete;
		ScopedTransaction& operator=(const ScopedTransaction&) = delete;

		// 適用した変更を確定する
		void Commit() noexcept { committed_ = true; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		static_assert(std::is_nothrow_swappable_v<T>);
		T& target_;				 // 変更対象
		T previous_;			 // 変更前の値
		bool committed_ = false; // 確定済みの状態
	};

	//============================================================================
	//	ScopedTransaction classTemplateMethods
	//============================================================================

	template <typename T>
	ScopedTransaction<T>::ScopedTransaction(T& target) : target_(target), previous_(target) {
	}

	template <typename T>
	ScopedTransaction<T>::~ScopedTransaction() {

		if (!committed_) {
			// 例外処理中の復元でメモリを確保しない
			using std::swap;
			swap(target_, previous_);
		}
	}
}
