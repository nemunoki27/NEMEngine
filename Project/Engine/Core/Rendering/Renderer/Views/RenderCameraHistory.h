#pragma once

//============================================================================
//	include
//============================================================================
#include "RenderViewTypes.h"

// c++
#include <string>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	RenderCameraHistory class
	//	Camera別に前frameの描画行列を保持する
	//============================================================================
	class RenderCameraHistory {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 同じframeの追加描画でも前frameの行列を維持する
		Matrix4x4 Update(const ResolvedRenderView& view, RenderCameraDomain domain, uint64_t frameSerial);
		// 所有元の終了時に履歴を破棄する
		void Clear() { states_.clear(); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct State {

			Matrix4x4 current = Matrix4x4::Identity();
			Matrix4x4 previous = Matrix4x4::Identity();
			uint64_t frameSerial = 0;
			uint32_t width = 0;
			uint32_t height = 0;
			ResolvedProjectionMode projectionMode = ResolvedProjectionMode::Perspective;
		};

		//--------- variables ----------------------------------------------------

		std::unordered_map<std::string, State> states_{};
		uint64_t cleanupFrameSerial_ = 0;
	};
} // Engine
