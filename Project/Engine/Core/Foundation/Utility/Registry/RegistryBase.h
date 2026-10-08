#pragma once

//============================================================================
//	include
//============================================================================
#include <vector>
#include <memory>
#include <unordered_map>

namespace Engine {

	// 登録内容の個体を区別する
	struct RegistryRevision final {};

	//============================================================================
	//	ListRegistryBase class
	//	登録順に項目を所有する共通基盤
	//============================================================================
	template <typename T>
	class ListRegistryBase {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		virtual ~ListRegistryBase() = default;

		// 登録
		virtual void Register(std::unique_ptr<T> item) {
			if (item) {
				auto revision = std::make_shared<const RegistryRevision>();
				items_.emplace_back(std::move(item));
				revision_ = std::move(revision);
			}
		}

		// クリア
		virtual void Clear() {
			auto revision = std::make_shared<const RegistryRevision>();
			// 登録順に所有項目を解放
			for (auto& item : items_) {
				item.reset();
			}
			items_.clear();
			revision_ = std::move(revision);
		}

		//--------- accessor -----------------------------------------------------

		// 登録された項目を借用する
		const std::vector<std::unique_ptr<T>>& GetItems() const { return items_; }
		// 登録内容の世代を借用する
		const std::shared_ptr<const RegistryRevision>& GetRevision() const { return revision_; }

	protected:
		//========================================================================
		//	protected Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 登録した項目の所有
		std::vector<std::unique_ptr<T>> items_;
		// 登録変更を区別する世代
		std::shared_ptr<const RegistryRevision> revision_ = std::make_shared<const RegistryRevision>();
	};

	//============================================================================
	//	MapRegistryBase class
	//	キーに対応する項目を所有する共通基盤
	//============================================================================
	template <typename Key, typename T>
	class MapRegistryBase {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		virtual ~MapRegistryBase() = default;

		// 登録
		virtual void Register(Key key, std::unique_ptr<T> item) {
			if (item) {
				items_.emplace(key, std::move(item));
			}
		}

		// クリア
		virtual void Clear() {
			// 現在の走査順で所有項目を解放
			for (auto& [key, item] : items_) {
				item.reset();
			}
			items_.clear();
		}

		// 検索
		virtual T* Find(Key key) {
			auto it = items_.find(key);
			return (it != items_.end()) ? it->second.get() : nullptr;
		}

		// 読み取り専用で検索する
		virtual const T* Find(Key key) const {
			auto it = items_.find(key);
			return (it != items_.end()) ? it->second.get() : nullptr;
		}

		//--------- accessor -----------------------------------------------------

		// キーと項目の対応を借用する
		const std::unordered_map<Key, std::unique_ptr<T>>& GetMap() const { return items_; }

	protected:
		//========================================================================
		//	protected Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 登録した項目の所有
		std::unordered_map<Key, std::unique_ptr<T>> items_;
	};

}
