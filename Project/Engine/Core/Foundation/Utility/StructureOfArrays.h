#pragma once

//============================================================================
//	include
//============================================================================
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace Engine {

	//============================================================================
	//	StructureOfArrays class
	//	指定したメンバーを独立した連続配列で保持する
	//============================================================================
	template<typename T, auto... Members>
	class StructureOfArrays {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		static constexpr size_t kElementBytes = (sizeof(std::remove_cvref_t<decltype(std::declval<T>().*Members)>) + ... + 0u);

		// 全列の要素数を揃える
		void Resize(size_t count) {
			std::apply([&](auto&... columns) { (columns.resize(count), ...); }, columns_);
		}
		// 全列の容量を確保する
		void Reserve(size_t count) {
			std::apply([&](auto&... columns) { (columns.reserve(count), ...); }, columns_);
		}
		// 一時評価用の値を取り出す
		T Get(size_t index) const {
			T value;
			GetMembers(value, index, std::index_sequence_for<decltype(Members)...>{});
			return value;
		}
		// 評価結果を各列へ戻す
		void Set(size_t index, const T& value) {
			SetMembers(value, index, std::index_sequence_for<decltype(Members)...>{});
		}
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::tuple<std::vector<std::remove_cvref_t<decltype(std::declval<T>().*Members)>>...> columns_;

		//--------- functions ----------------------------------------------------

		// メンバーと列の順序を対応させる
		template<size_t... Indices>
		void GetMembers(T& value, size_t index, std::index_sequence<Indices...>) const {
			((value.*Members = std::get<Indices>(columns_)[index]), ...);
		}
		// 同じ対応表で書き戻す
		template<size_t... Indices>
		void SetMembers(const T& value, size_t index, std::index_sequence<Indices...>) {
			((std::get<Indices>(columns_)[index] = value.*Members), ...);
		}
	};
}
