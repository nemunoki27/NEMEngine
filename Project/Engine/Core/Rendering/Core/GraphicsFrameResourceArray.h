#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>

// c++
#include <array>
#include <utility>
#include <memory>

namespace Engine {

	//============================================================================
	//	GraphicsFrameResourceArray class
	//	未使用フレームの資源とプール領域を所有者の寿命によらず解放する
	//============================================================================
	template<typename T>
	class GraphicsFrameResourceArray {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		GraphicsFrameResourceArray() = default;
		GraphicsFrameResourceArray(const GraphicsFrameResourceArray&) = delete;
		GraphicsFrameResourceArray& operator=(const GraphicsFrameResourceArray&) = delete;
		GraphicsFrameResourceArray(GraphicsFrameResourceArray&& other) noexcept { Swap(other); }
		GraphicsFrameResourceArray& operator=(GraphicsFrameResourceArray&& other) noexcept {
			if (this != &other) { values_ = {}; Swap(other); }
			return *this;
		}

		// GPU資源を確保する前に回収先へ接続する
		void Connect(GraphicsResourceRetirement& retirement) {
			registration_.Bind(retirement.GetFrameResources(), this, [](void* owner, uint32_t count) {
				auto& values = static_cast<GraphicsFrameResourceArray*>(owner)->values_;
				for (uint32_t index = count; index < kGraphicsFrameContextCount; ++index) {
					std::destroy_at(&values[index]);
					std::construct_at(&values[index]);
				}
			});
		}

		T& operator[](size_t index) { return values_[index]; }
		const T& operator[](size_t index) const { return values_[index]; }
		auto begin() { return values_.begin(); }
		auto end() { return values_.end(); }
		auto begin() const { return values_.begin(); }
		auto end() const { return values_.end(); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::array<T, kGraphicsFrameContextCount> values_{};
		GraphicsFrameResourceRegistration registration_{};

		//--------- functions ----------------------------------------------------

		// 移動後の配列へ通知先を付け替える
		void Swap(GraphicsFrameResourceArray& other) noexcept {
			values_.swap(other.values_);
			registration_.Swap(other.registration_, this, &other);
		}
	};
}
