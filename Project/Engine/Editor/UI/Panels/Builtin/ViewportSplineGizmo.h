#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Transform/SplineComponent.h>
#include <imgui.h>

namespace Engine {

	struct EditorPanelContext;

	class ViewportSplineGizmo {
	public:

		~ViewportSplineGizmo();
		// 選択曲線の制御点と接線をScene画像上で操作する
		bool Draw(const EditorPanelContext& context, ImVec2 imagePosition, ImVec2 imageSize);
		// 未確定の制御点を開始時の保存値へ戻す
		void EndPreview();
	private:

		ECSWorld* world_ = nullptr;
		std::weak_ptr<const ECSWorldLifetime> lifetime_;
		UUID owner_{};
		nlohmann::json before_;
		SplineData draft_;
		int32_t point_ = -1;
		uint32_t part_ = 0;
		float depth_ = 0;
	};
}
