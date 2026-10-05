#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <array>
#include <cstddef>

namespace Engine {

	//============================================================================
	//	FrameProfileHistory class
	//	フレーム内の累積と確定済み八フレームの平均を保持する
	//============================================================================
	class FrameProfileHistory {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 前フレームの累積を履歴へ確定する
		void BeginFrame(bool firstFrame);

		//--------- accessor -----------------------------------------------------

		void Add(float milliseconds) { accumulator_ += milliseconds; }
		float GetAverage() const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		static constexpr size_t kSampleCount = 8;
		// 現在の累積と確定済みフレームの循環履歴
		float accumulator_ = 0.0f;
		std::array<float, kSampleCount> samples_{};
		size_t nextSample_ = 0;
		size_t sampleCount_ = 0;
	};
} // Engine
