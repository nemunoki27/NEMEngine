#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Time/FrameProfiler.h>

// c++
#include <span>
#include <vector>

namespace Engine {

	// View別の計測行と未計測の開始時刻
	struct ConsoleGPUPassViewSnapshot {

		struct Pass {

			FrameProfiler::GPUPassTime time;
			double missingSince = -1.0;
		};
		std::string name{};
		std::vector<Pass> passTimes{};
		float totalMilliseconds = 0.0f;
	};

	//============================================================================
	//	ConsoleGPUPassTooltipState class
	//	計測行の保持と期限切れを管理する
	//============================================================================
	class ConsoleGPUPassTooltipState {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 新しいGPU結果だけを取り込み実時間で行を整理する
		void Update(std::span<const FrameProfiler::GPUPassTime> times, uint64_t frameID, bool hasData, double now);

		//--------- accessor -----------------------------------------------------

		const std::vector<ConsoleGPUPassViewSnapshot>& GetViews() const { return views_; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::vector<ConsoleGPUPassViewSnapshot> views_;
		uint64_t lastFrameID_ = UINT64_MAX;
	};

	// GPU計測一覧を更新し必要な場合だけ表示する
	void DrawConsoleGPUPassTooltip(const FrameProfiler& profiler, bool hovered);
}
