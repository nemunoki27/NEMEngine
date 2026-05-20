#include "NodeGraphStyle.h"

//============================================================================
//	NodeGraphStyle classMethods
//============================================================================

ImVec4 Engine::NodeGraphStyle::GetNodeAccentColor(const std::string& nodeType) const {

	// RenderPathの主なPass種別ごとにNode色を分ける
	if (nodeType.find("Clear") != std::string::npos) {
		return ImVec4(0.35f, 0.50f, 0.72f, 1.0f);
	}
	if (nodeType.find("DepthPrepass") != std::string::npos) {
		return ImVec4(0.58f, 0.42f, 0.85f, 1.0f);
	}
	if (nodeType.find("Draw") != std::string::npos || nodeType.find("RenderScene") != std::string::npos) {
		return ImVec4(0.34f, 0.72f, 0.42f, 1.0f);
	}
	if (nodeType.find("Compute") != std::string::npos) {
		return ImVec4(0.25f, 0.75f, 0.80f, 1.0f);
	}
	if (nodeType.find("PostProcess") != std::string::npos) {
		return ImVec4(0.78f, 0.34f, 0.76f, 1.0f);
	}
	if (nodeType.find("Blit") != std::string::npos || nodeType.find("FullscreenCopy") != std::string::npos) {
		return ImVec4(0.85f, 0.55f, 0.25f, 1.0f);
	}
	if (nodeType.find("Raytracing") != std::string::npos) {
		return ImVec4(0.78f, 0.25f, 0.28f, 1.0f);
	}
	if (nodeType.find("Temporary") != std::string::npos) {
		return ImVec4(0.85f, 0.76f, 0.25f, 1.0f);
	}
	if (nodeType.find("View") != std::string::npos) {
		return ImVec4(0.95f, 0.80f, 0.35f, 1.0f);
	}
	return ImVec4(0.45f, 0.45f, 0.48f, 1.0f);
}

ImVec4 Engine::NodeGraphStyle::GetPinColor(GraphValueType valueType) const {

	// Pinの型が近いものは同じ色にして、接続できる種類を見分けやすくする
	switch (valueType) {
	case GraphValueType::Flow:
		return ImVec4(1.00f, 0.74f, 0.30f, 1.0f);
	case GraphValueType::Texture2D:
	case GraphValueType::Texture2DUAV:
	case GraphValueType::RenderTarget:
		return ImVec4(0.35f, 0.68f, 0.95f, 1.0f);
	case GraphValueType::DepthTexture:
		return ImVec4(0.58f, 0.45f, 0.90f, 1.0f);
	case GraphValueType::MaterialAsset:
	case GraphValueType::ShaderAsset:
	case GraphValueType::Asset:
		return ImVec4(0.75f, 0.55f, 0.95f, 1.0f);
	case GraphValueType::View:
		return ImVec4(0.95f, 0.85f, 0.45f, 1.0f);
	default:
		return ImVec4(0.70f, 0.70f, 0.72f, 1.0f);
	}
}

ImVec4 Engine::NodeGraphStyle::GetLinkColor(GraphValueType valueType) const {

	// LinkはPin色を少し透過させて使用する
	ImVec4 color = GetPinColor(valueType);
	color.w = 0.90f;
	return color;
}

ImVec4 Engine::NodeGraphStyle::GetErrorColor() const {

	// Validation Error表示色
	return ImVec4(0.95f, 0.28f, 0.25f, 1.0f);
}

ImVec4 Engine::NodeGraphStyle::GetWarningColor() const {

	// Validation Warning表示色
	return ImVec4(0.95f, 0.70f, 0.24f, 1.0f);
}
