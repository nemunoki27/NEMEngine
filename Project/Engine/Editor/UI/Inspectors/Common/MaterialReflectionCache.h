#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>

// c++
#include <memory>
#include <cstdint>

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

		// Materialの既定値と描画用reflectionを解決する
		const ShaderReflectionInfo* EnsureReflection(const EditorPanelContext& context,
			AssetID materialID, AssetID defaultMaterialID);
		// InstanceのIDと用途を優先して実効値を解決する
		MaterialParameterValue ResolveValue(const MaterialInstanceParameters& parameters,
			const ShaderConstantBufferVariable& var) const;

		//--------- accessor -----------------------------------------------------

		const MaterialAsset& GetMaterial() const { return cachedMaterial_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 同じIDでも索引の切替と内容更新を区別する
		std::weak_ptr<const uint8_t> cachedDatabaseLifetime_;
		uint64_t cachedDatabaseRevision_ = 0;
		uint64_t cachedMaterialRevision_ = 0;
		AssetID cachedMaterialID_{};
		MaterialAsset cachedMaterial_{};
		bool cachedMaterialValid_ = false;
	};
}
