#include "RendererRenderSettings.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Externals/nlohmann/json.hpp>

// c++
#include <algorithm>
#include <stdexcept>

namespace {

	template<class T>
	void ReadEnum(const nlohmann::json& in, const char* key, T& value) {

		const auto it = in.find(key);
		if (it == in.end()) return;
		const auto parsed = Engine::EnumAdapter<T>::FromString(it->get<std::string>());
		if (!parsed) throw std::invalid_argument(key);
		value = *parsed;
	}

	uint8_t ReadByte(const nlohmann::json& in, const char* key, uint8_t fallback) {

		return static_cast<uint8_t>(std::clamp(in.value(key, int64_t(fallback)), int64_t(0), int64_t(255)));
	}
}

void Engine::from_json(const nlohmann::json& in, RendererStencilFace& value) {

	if (!in.is_object()) throw std::invalid_argument("RendererStencilFace");
	ReadEnum(in, "comparison", value.comparison);
	ReadEnum(in, "fail", value.fail);
	ReadEnum(in, "depthFail", value.depthFail);
	ReadEnum(in, "pass", value.pass);
}

void Engine::to_json(nlohmann::json& out, const RendererStencilFace& value) {

	out["comparison"] = EnumAdapter<RendererDepthComparison>::ToString(value.comparison);
	out["fail"] = EnumAdapter<RendererStencilOperation>::ToString(value.fail);
	out["depthFail"] = EnumAdapter<RendererStencilOperation>::ToString(value.depthFail);
	out["pass"] = EnumAdapter<RendererStencilOperation>::ToString(value.pass);
}

void Engine::from_json(const nlohmann::json& in, RendererRenderSettings& value) {

	if (!in.is_object()) throw std::invalid_argument("RendererRenderSettings");
	RendererRenderSettings candidate = value;
	candidate.inheritMaterial = in.value("inheritMaterial", candidate.inheritMaterial);
	ReadEnum(in, "fillMode", candidate.fillMode);
	ReadEnum(in, "cullMode", candidate.cullMode);
	candidate.depthTest = in.value("depthTest", candidate.depthTest);
	candidate.depthWrite = in.value("depthWrite", candidate.depthWrite);
	candidate.stencilEnabled = in.value("stencilEnabled", candidate.stencilEnabled);
	candidate.separateBackFace = in.value("separateBackFace", candidate.separateBackFace);
	ReadEnum(in, "depthComparison", candidate.depthComparison);
	candidate.stencilReference = ReadByte(in, "stencilReference", candidate.stencilReference);
	candidate.stencilReadMask = ReadByte(in, "stencilReadMask", candidate.stencilReadMask);
	candidate.stencilWriteMask = ReadByte(in, "stencilWriteMask", candidate.stencilWriteMask);
	if (const auto it = in.find("frontFace"); it != in.end()) from_json(*it, candidate.frontFace);
	if (const auto it = in.find("backFace"); it != in.end()) from_json(*it, candidate.backFace);
	value = candidate;
}

void Engine::to_json(nlohmann::json& out, const RendererRenderSettings& value) {

	out["inheritMaterial"] = value.inheritMaterial;
	out["fillMode"] = EnumAdapter<RendererFillMode>::ToString(value.fillMode);
	out["cullMode"] = EnumAdapter<RendererCullMode>::ToString(value.cullMode);
	out["depthTest"] = value.depthTest;
	out["depthWrite"] = value.depthWrite;
	out["stencilEnabled"] = value.stencilEnabled;
	out["separateBackFace"] = value.separateBackFace;
	out["depthComparison"] = EnumAdapter<RendererDepthComparison>::ToString(value.depthComparison);
	out["stencilReference"] = value.stencilReference;
	out["stencilReadMask"] = value.stencilReadMask;
	out["stencilWriteMask"] = value.stencilWriteMask;
	out["frontFace"] = value.frontFace;
	out["backFace"] = value.backFace;
}
