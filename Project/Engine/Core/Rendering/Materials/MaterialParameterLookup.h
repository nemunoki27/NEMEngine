#pragma once

//============================================================================
//	include
//============================================================================
#include "MaterialParameter.h"

namespace Engine {
	struct ShaderConstantBufferVariable;
	struct ShaderResourceBinding;
	struct ShaderReflectionInfo;
}

namespace Engine::MaterialParameterLookup {

	// IDと意味と表示名の順に値を検索する
	const MaterialParameterValue* Find(const MaterialParameterSet& parameters, MaterialParameterID id,
		MaterialParameterSemantic semantic, std::string_view name, const MaterialParameterSet* defaults = nullptr);
	// パラメータが指定Assetを参照するか調べる
	bool ReferencesAsset(const MaterialParameterSet& parameters, AssetID assetID);
	// 標準MaterialのTexture用途を判定する
	bool IsTextureSemantic(MaterialParameterSemantic semantic);
	// Material用のTexture参照を判定する
	bool IsTextureResource(const ShaderResourceBinding& resource);
	// 公開IDと意味を優先して同じ入力を判定する
	bool IsSameParameter(const ShaderConstantBufferVariable& variable, const ShaderResourceBinding& resource);
	// Metadataと既知の用途とSRVからTexture番号を判定する
	bool IsTexture(const ShaderConstantBufferVariable& variable, const ShaderReflectionInfo& reflection);
}
