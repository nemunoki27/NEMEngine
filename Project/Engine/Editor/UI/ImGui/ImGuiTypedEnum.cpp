#include "ImGuiEnum.h"

//============================================================================
//	ImGuiUtility namespaceMethods
//============================================================================
const char* Engine::ImGuiUtility::GetEnumDisplayName(std::string_view type, const char* name) {

	if (type.ends_with("SceneViewCameraControlStyle") && std::string_view(name) == "Original") return "オリジナル";
	if (type.ends_with("SceneViewCameraControlSource") && std::string_view(name) == "Disabled") return "使用しない";

	// ブレンドの標準値とパーティクルの法線方向を区別する
	if (type.ends_with("BlendMode") && std::string_view(name) == "Normal") return "標準";
	if (type.ends_with("ParticleForceFieldType")) {
		if (std::string_view(name) == "Attraction") return "引力";
		if (std::string_view(name) == "Repulsion") return "斥力";
		if (std::string_view(name) == "Vortex") return "渦";
		if (std::string_view(name) == "Wind") return "風";
	}
	if (type.ends_with("ParticleSourceKind")) {
		if (std::string_view(name) == "Analytic") return "基本形状";
		if (std::string_view(name) == "MeshVertex") return "メッシュ頂点";
		if (std::string_view(name) == "MeshEdge") return "メッシュ辺";
		if (std::string_view(name) == "MeshSurface") return "メッシュ表面";
		if (std::string_view(name) == "Texture") return "画像";
		if (std::string_view(name) == "CameraSurface") return "Camera表面";
	}
	if (type.ends_with("ParticleSourceChannel")) {
		if (std::string_view(name) == "Red") return "赤";
		if (std::string_view(name) == "Green") return "緑";
		if (std::string_view(name) == "Blue") return "青";
		if (std::string_view(name) == "Luminance") return "輝度";
	}
	if (type.ends_with("ParticleVectorFieldPreset")) {
		if (std::string_view(name) == "Wind") return "風";
		if (std::string_view(name) == "Vortex") return "渦";
		if (std::string_view(name) == "Asset") return "FGA";
	}
	if (type.ends_with("ParticleCollisionResponse")) {
		if (std::string_view(name) == "Bounce") return "反射";
		if (std::string_view(name) == "Stop") return "停止";
		if (std::string_view(name) == "Kill") return "消滅";
	}
	return GetDisplayName(name);
}
