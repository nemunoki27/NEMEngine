#include "ImGuiEnum.h"

//============================================================================
//	ImGuiUtility namespaceMethods
//============================================================================
const char* Engine::ImGuiUtility::GetEnumDisplayName(std::string_view type, const char* name) {

	if (type.ends_with("EasingType") || type.ends_with("RendererFillMode") ||
		type.ends_with("RendererCullMode") || type.ends_with("RendererDepthComparison") ||
		type.ends_with("RendererStencilOperation") || type.ends_with("BlendMode") ||
		type.ends_with("RenderPhase") || type.ends_with("MaterialSurfaceMode") ||
		type.ends_with("ShaderGraphSurfaceMode") || type.ends_with("MaterialCreateType") ||
		type.ends_with("PrimitiveType") || type.ends_with("ParticleEmitterShape") ||
		type.ends_with("ColliderShapeType")) return name;
	if (type.ends_with("PrimitiveRenderSpace") || type.ends_with("Dimension") ||
		type.ends_with("SceneViewMode")) {
		const std::string_view value = name;
		if (value == "Screen2D" || value == "Type2D" || value == "Mode2D") return "2D";
		if (value == "World3D" || value == "Type3D" || value == "Mode3D") return "3D";
	}
	// DirectXの選択肢は型の接頭辞だけを除いて英語で表示する
	if (type.starts_with("D3D12_")) {
		for (const std::string_view prefix : { "D3D12_TEXTURE_ADDRESS_MODE_", "D3D12_TEXTURE_ADDRESS_",
			"D3D12_FILL_MODE_", "D3D12_CULL_MODE_", "D3D12_COMPARISON_FUNC_", "D3D12_COMPARISON_",
			"D3D12_FILTER_", "D3D12_DEPTH_WRITE_MASK_", "D3D12_STATIC_BORDER_COLOR_", "D3D12_STENCIL_OP_" }) {
			if (std::string_view(name).starts_with(prefix)) return name + prefix.size();
		}
		return name;
	}
	if (type.ends_with("AntialiasingMethod")) {
		if (std::string_view(name) == "None") return "なし";
		return name;
	}
	if (type.ends_with("SceneViewCameraControlStyle") && std::string_view(name) == "Original") return "オリジナル";
	if (type.ends_with("SceneViewCameraControlSource") && std::string_view(name) == "Disabled") return "使用しない";

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
