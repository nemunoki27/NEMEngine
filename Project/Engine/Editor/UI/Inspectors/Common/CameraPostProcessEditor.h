#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfile.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

namespace Engine {

	//============================================================================
	//	CameraPostProcessEditor class
	//	Cameraの露出と色補正を編集するクラス
	//============================================================================
	class CameraPostProcessEditor final {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 各項目の編集結果を返す
		static ValueEditResult Draw(ColorPipelineSettings& settings);
	};
} // Engine
