#pragma once

//============================================================================
//	include
//============================================================================

namespace Engine::RenderPathGraph {

	//============================================================================
	//	RenderPathGraphTypes constants
	//	RenderPath Graphで使用するNode種別名
	//============================================================================

	// GraphDocument.graphType
	inline constexpr const char* kGraphType = "RenderPath";

	// ScenePass系Node
	inline constexpr const char* kClear = "RenderPath.Clear";
	inline constexpr const char* kDepthPrepass = "RenderPath.DepthPrepass";
	inline constexpr const char* kDraw = "RenderPath.Draw";
	inline constexpr const char* kCompute = "RenderPath.Compute";
	inline constexpr const char* kPostProcess = "RenderPath.PostProcess";
	inline constexpr const char* kBlit = "RenderPath.Blit";
	inline constexpr const char* kRaytracing = "RenderPath.Raytracing";
	inline constexpr const char* kRenderScene = "RenderPath.RenderScene";
	inline constexpr const char* kFullscreenCopy = "RenderPath.FullscreenCopy";
	// Resource / 補助Node
	inline constexpr const char* kTemporaryTarget = "RenderPath.TemporaryTarget";
	inline constexpr const char* kRenderTarget = "RenderPath.RenderTarget";
	inline constexpr const char* kView = "RenderPath.View";
	inline constexpr const char* kComment = "RenderPath.Comment";
	inline constexpr const char* kReroute = "RenderPath.Reroute";
	inline constexpr const char* kUnknown = "RenderPath.Unknown";
}