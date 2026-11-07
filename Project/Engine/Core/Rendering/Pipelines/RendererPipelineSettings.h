#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/RendererRenderSettings.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>

namespace Engine {

	// 描画設定からPSOに影響する項目だけを取り出す
	RendererRenderSettings NormalizeRendererPipelineSettings(const RendererRenderSettings* settings);
	// 個別の描画設定をパイプラインへ適用する
	void ApplyRendererPipelineSettings(GraphicsPipelineDesc& desc, const RendererRenderSettings& settings);
}
