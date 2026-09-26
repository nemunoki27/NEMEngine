#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>

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
		//============================================================================
		//	public Methods
		//============================================================================

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
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		struct Entry {

			std::unique_ptr<T> resource;
			uint64_t lastUsedSerial = UINT64_MAX;
		};
		//--------- variables ----------------------------------------------------

		std::vector<Entry> resources_{};
		size_t usedCount_ = 0;
		uint64_t frameSerial_ = UINT64_MAX;
	};

	//============================================================================
	//	FrameBatchResourcePool templateMethods
	//============================================================================
	template<typename T>
	inline void FrameBatchResourcePool<T>::BeginFrame() {

		// 同じframeのView切替では先行Batchを再利用しない
		const uint64_t serial = GraphicsFrameState::GetFrameSerial();
		if (frameSerial_ == serial) return;
		// 長期間使っていない末尾のBatchを所有元から外す
		while (!resources_.empty() && HasExpiredGraphicsResource(resources_.back().lastUsedSerial, serial)) {
			resources_.pop_back();
		}
		frameSerial_ = serial;
		usedCount_ = 0;
	}

	template<typename T>
	inline void FrameBatchResourcePool<T>::Clear() {

		resources_.clear();
		usedCount_ = 0;
		frameSerial_ = UINT64_MAX;
	}

	template<typename T>
	template<typename Fn>
	inline T& FrameBatchResourcePool<T>::Acquire(GraphicsCore& graphicsCore, Fn&& fn) {
	
		// 使用数がリソースの数を超える場合は新しいリソースを作成
		if (resources_.size() <= usedCount_) {

			std::unique_ptr<T> resource = std::make_unique<T>();
			fn(*resource, graphicsCore);
			resources_.push_back({ std::move(resource), GraphicsFrameState::GetFrameSerial() });
		}

		// 使用数をインクリメントしてリソースを返す
		auto& entry = resources_[usedCount_];
		entry.lastUsedSerial = GraphicsFrameState::GetFrameSerial();
		T& resource = *entry.resource;
		++usedCount_;
		return resource;
	}
} // Engine

