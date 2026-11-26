#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/SurfaceLightingSettings.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

namespace Engine {

	class GraphicsCore;
	class RenderAssetLibrary;

	//============================================================================
	//	CameraSurfaceSettingsEditor class
	//	Cameraの反射と遮蔽の編集結果を返す
	//============================================================================
	class CameraSurfaceSettingsEditor final {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 反射の設定と移行済みパラメータを編集する
		static ValueEditResult DrawReflection(SurfaceLightingSettings& settings, RenderAssetLibrary* library);
		// 対応GPUで遮蔽の設定を編集する
		static ValueEditResult DrawAmbientOcclusion(SurfaceLightingSettings& settings, GraphicsCore* graphicsCore);
	};
}
