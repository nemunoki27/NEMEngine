#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>

namespace Engine {

	struct EditorPanelContext;
	struct ShaderReflectionInfo;
	struct ShaderConstantBufferVariable;

	//============================================================================
	//	MaterialReflectionCache class
	//	編集中Materialのreflectionと実効パラメータを解決するクラス
	//============================================================================
	class MaterialReflectionCache {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		const ShaderReflectionInfo* EnsureReflection(const EditorPanelContext& context,
			AssetID materialID, AssetID defaultMaterialID);
		MaterialParameterValue ResolveValue(const MaterialInstanceParameters& parameters,
			const ShaderConstantBufferVariable& var) const;

		//--------- accessor -----------------------------------------------------

		const MaterialAsset& GetMaterial() const { return cachedMaterial_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		AssetID cachedMaterialID_{};
		MaterialAsset cachedMaterial_{};
		bool cachedMaterialValid_ = false;
	};
}
