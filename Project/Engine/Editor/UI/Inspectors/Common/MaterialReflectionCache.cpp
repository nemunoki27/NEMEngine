#include "MaterialReflectionCache.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Editor/UI/Common/MaterialParameterEditor.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

// c++
#include <filesystem>

const Engine::ShaderReflectionInfo* Engine::MaterialReflectionCache::EnsureReflection(
	const EditorPanelContext& context, AssetID materialID, AssetID defaultMaterialID) {

	if (!context.renderPipeline || !context.editorContext || !context.editorContext->assetDatabase) {
		return nullptr;
	}
	// 空マテリアルは描画時にデフォルトへ解決されるので、reflectionも実効デフォルトから引く
	if (!materialID) {
		materialID = defaultMaterialID;
	}
	// マテリアルが変わったときだけファイルを読み直す
	if (!cachedMaterialValid_ || cachedMaterialID_ != materialID) {

		cachedMaterialValid_ = false;
		cachedMaterialID_ = materialID;
		cachedMaterial_ = MaterialAsset{};
		const std::filesystem::path path = context.editorContext->assetDatabase->ResolveFullPath(materialID);
		if (!path.empty()) {

			nlohmann::json data = JsonAdapter::Load(path.string(), false);
			cachedMaterialValid_ = FromJson(data, cachedMaterial_);
		}
	}
	if (!cachedMaterialValid_) {
		return nullptr;
	}
	return context.renderPipeline->FindMaterialDrawReflection(cachedMaterial_);
}

Engine::MaterialParameterValue Engine::MaterialReflectionCache::ResolveValue(
	const MaterialInstanceParameters& parameters, const ShaderConstantBufferVariable& var) const {

	if (const MaterialParameterValue* value =
		parameters.Find(var.parameterID)) {

		return *value;
	}
	if (var.semantic != MaterialParameterSemantic::None) {

		if (const MaterialParameterValue* value =
			parameters.Find(var.semantic)) {

			return *value;
		}
	}
	if (const MaterialParameterValue* value =
		cachedMaterial_.parameters.Find(var.parameterID)) {

		return *value;
	}
	if (var.semantic != MaterialParameterSemantic::None) {

		if (const MaterialParameterValue* value =
			cachedMaterial_.parameters.Find(var.semantic)) {

			return *value;
		}
	}
	return MaterialParameterEditor::DefaultValueForVariable(var);
}
