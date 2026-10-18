#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>

namespace Engine {

	class AssetDatabase;

	namespace DecalMaterialFields {

		// 標準値とInstanceの上書きを同じUIで編集する
		ValueEditResult Draw(MaterialParameterSet& parameters, const MaterialParameterSet& defaults, const AssetDatabase* database);
	}
}
