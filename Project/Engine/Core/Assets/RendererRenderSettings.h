#pragma once

//============================================================================
//	include
//============================================================================
#include <Externals/nlohmann/json_fwd.hpp>

// c++
#include <cstdint>

namespace Engine {

	enum class RendererFillMode : uint8_t { WireFrame = 2, Solid = 3 };
	enum class RendererCullMode : uint8_t { NONE = 1, FRONT = 2, BACK = 3 };
	enum class RendererDepthComparison : uint8_t {

		NEVER = 1, LESS, EQUAL, LESS_EQUAL, GREATER, NOT_EQUAL, GREATER_EQUAL, ALWAYS,
	};
	enum class RendererStencilOperation : uint8_t {

		KEEP = 1, ZERO, REPLACE, INCR_SAT, DECR_SAT, INVERT, INCR, DECR,
	};

	struct RendererStencilFace {

		RendererDepthComparison comparison = RendererDepthComparison::ALWAYS;
		RendererStencilOperation fail = RendererStencilOperation::KEEP;
		RendererStencilOperation depthFail = RendererStencilOperation::KEEP;
		RendererStencilOperation pass = RendererStencilOperation::KEEP;

		bool operator==(const RendererStencilFace&) const = default;
	};

	// Renderer単位で適用する描画状態
	struct RendererRenderSettings {

		bool inheritMaterial = true;
		RendererFillMode fillMode = RendererFillMode::Solid;
		RendererCullMode cullMode = RendererCullMode::BACK;
		bool depthTest = true;
		bool depthWrite = true;
		bool stencilEnabled = false;
		bool separateBackFace = false;
		RendererDepthComparison depthComparison = RendererDepthComparison::LESS_EQUAL;
		uint8_t stencilReference = 1;
		uint8_t stencilReadMask = 255;
		uint8_t stencilWriteMask = 255;
		RendererStencilFace frontFace{};
		RendererStencilFace backFace{};

		bool operator==(const RendererRenderSettings&) const = default;
	};

	// 描画設定をJSONへ保存して読み戻す
	void from_json(const nlohmann::json& in, RendererStencilFace& value);
	void to_json(nlohmann::json& out, const RendererStencilFace& value);
	void from_json(const nlohmann::json& in, RendererRenderSettings& value);
	void to_json(nlohmann::json& out, const RendererRenderSettings& value);
} // Engine
