#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/GraphicsCore.h>

#include <memory>
#include <vector>
#include <utility>

namespace Engine {

	//============================================================================
	//	FrameBatchResourcePool class
	//	フレーム内でバッチ処理で使用するリソースのプール
	//============================================================================
	template <typename T>
	class FrameBatchResourcePool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		FrameBatchResourcePool() = default;
		~FrameBatchResourcePool() = default;

		// フレーム開始処理
		void BeginFrame();

		// フレーム内の次のバッチ処理のリソースを取得
		template <typename Fn>
		T& Acquire(GraphicsCore& graphicsCore, Fn&& fn);

		// データクリア
		void Clear();

		//--------- accessor -----------------------------------------------------

		// 使用数
		size_t GetUsedCount() const { return usedCount_; }
		size_t GetCapacity() const { return resources_.size(); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::vector<std::unique_ptr<T>> resources_{};
		size_t usedCount_ = 0;
	};

	//============================================================================
	//	FrameBatchResourcePool templateMethods
	//============================================================================

	template<typename T>
	inline void FrameBatchResourcePool<T>::BeginFrame() {

		// フレーム開始時に使用数をリセット
		usedCount_ = 0;
	}

	template<typename T>
	inline void FrameBatchResourcePool<T>::Clear() {

		resources_.clear();
		usedCount_ = 0;
	}

	template<typename T>
	template<typename Fn>
	inline T& FrameBatchResourcePool<T>::Acquire(GraphicsCore& graphicsCore, Fn&& fn) {
	
		// 使用数がリソースの数を超える場合は新しいリソースを作成
		if (resources_.size() <= usedCount_) {

			resources_.emplace_back(std::make_unique<T>());
		}

		// 使用数をインクリメントしてリソースを返す
		T& resource = *resources_[usedCount_];
		++usedCount_;
		// リソースの初期化関数を呼び出す
		fn(resource, graphicsCore);
		return resource;
	}
} // Engine