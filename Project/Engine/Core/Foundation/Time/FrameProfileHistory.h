#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <list>

namespace Engine {

	//============================================================================
	//	FrameProfileHistory class
	//	フレーム内の累積と確定済み八フレームの平均を保持する
	//============================================================================
	class FrameProfileHistory {
	public:

		void BeginFrame(bool firstFrame);
		void Add(float milliseconds) { accumulator_ += milliseconds; }
		float GetAverage() const;
	private:

		//--------- variables ----------------------------------------------------

		float accumulator_ = 0.0f;
		std::list<float> samples_;
	};
}
