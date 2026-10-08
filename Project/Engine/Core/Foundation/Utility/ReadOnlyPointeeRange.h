#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <iterator>
#include <memory>
#include <ranges>
#include <type_traits>
#include <utility>

namespace Engine {

	//============================================================================
	//	ReadOnlyPointeeRange class
	//	所有元の存続中にポインタ先を読取専用で列挙する
	//============================================================================
	template <typename Container>
	class ReadOnlyPointeeRange : public std::ranges::view_interface<ReadOnlyPointeeRange<Container>> {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 所有ポインタを公開しない列挙位置
		class Iterator {
			friend class ReadOnlyPointeeRange;

		public:
			using UnderlyingIterator = typename Container::const_iterator;
			using value_type = std::remove_cvref_t<decltype(**std::declval<UnderlyingIterator>())>;
			using difference_type = typename std::iterator_traits<UnderlyingIterator>::difference_type;
			using reference = const value_type&;
			using pointer = const value_type*;
			using iterator_category = std::forward_iterator_tag;
			using iterator_concept = std::forward_iterator_tag;

			Iterator() = default;
			reference operator*() const { return **current_; }
			pointer operator->() const { return std::addressof(**current_); }
			Iterator& operator++() {
				++current_;
				return *this;
			}
			Iterator operator++(int) {
				Iterator previous = *this;
				++current_;
				return previous;
			}
			bool operator==(const Iterator&) const = default;

		private:
			//====================================================================
			//	private Methods
			//====================================================================

			//--------- variables ------------------------------------------------

			UnderlyingIterator current_{}; // 借用した一覧の位置

			//--------- functions ------------------------------------------------

			// 一覧の開始または終端を保持する
			explicit Iterator(UnderlyingIterator current) : current_(current) {}
		};

		// 要素がnullでない一覧を借用する
		explicit ReadOnlyPointeeRange(const Container& container) : container_(std::addressof(container)) {}
		ReadOnlyPointeeRange(Container&&) = delete;
		ReadOnlyPointeeRange(const Container&&) = delete;
		Iterator begin() const { return Iterator(container_->cbegin()); }
		Iterator end() const { return Iterator(container_->cend()); }
		auto size() const { return container_->size(); }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		const Container* container_; // 一覧の借用元
	};
}
