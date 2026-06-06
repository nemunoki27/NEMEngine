#pragma once

//============================================================================
//	include
//============================================================================
#include <vector>
#include <memory>
#include <unordered_map>

namespace Engine {

	// リスト形式のレジストリ基底クラス
	template <typename T>
	class ListRegistryBase {
	public:
		virtual ~ListRegistryBase() = default;

		// 登録
		virtual void Register(std::unique_ptr<T> item) {
			if (item) {
				items_.emplace_back(std::move(item));
			}
		}

		// クリア
		virtual void Clear() {
			// unique_ptrはclear任せにせず、終了経路で明示的にresetして所有リソースを解放する
			for (auto& item : items_) {
				item.reset();
			}
			items_.clear();
		}

		// アイテムリストの取得
		const std::vector<std::unique_ptr<T>>& GetItems() const { return items_; }

	protected:
		std::vector<std::unique_ptr<T>> items_;
	};

	// マップ形式のレジストリ基底クラス
	template <typename Key, typename T>
	class MapRegistryBase {
	public:
		virtual ~MapRegistryBase() = default;

		// 登録
		virtual void Register(Key key, std::unique_ptr<T> item) {
			if (item) {
				items_.emplace(key, std::move(item));
			}
		}

		// クリア
		virtual void Clear() {
			// unique_ptrはclear任せにせず、終了経路で明示的にresetして所有リソースを解放する
			for (auto& [key, item] : items_) {
				(void)key;
				item.reset();
			}
			items_.clear();
		}

		// 検索
		virtual T* Find(Key key) {
			auto it = items_.find(key);
			return (it != items_.end()) ? it->second.get() : nullptr;
		}

		// 検索 (const)
		virtual const T* Find(Key key) const {
			auto it = items_.find(key);
			return (it != items_.end()) ? it->second.get() : nullptr;
		}

		// マップの取得
		const std::unordered_map<Key, std::unique_ptr<T>>& GetMap() const { return items_; }

	protected:
		std::unordered_map<Key, std::unique_ptr<T>> items_;
	};

} // Engine
