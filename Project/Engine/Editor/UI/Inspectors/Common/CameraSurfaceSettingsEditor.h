#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

namespace Engine {

	class GraphicsCore;

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
		static ValueEditResult DrawReflection(CameraCommon& settings);
		// 対応GPUで遮蔽の設定を編集する
		static ValueEditResult DrawAmbientOcclusion(CameraCommon& settings, GraphicsCore* graphicsCore);
	};
}
