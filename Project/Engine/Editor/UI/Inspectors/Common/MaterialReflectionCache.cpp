#include "MaterialReflectionCache.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterDefaults.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>

// c++
#include <filesystem>

//============================================================================
//	MaterialReflectionCache classMethods
//============================================================================
const Engine::ShaderReflectionInfo* Engine::MaterialReflectionCache::EnsureReflection(
	const EditorPanelContext& context, AssetID materialID, AssetID defaultMaterialID) {

	if (!context.renderPipeline || !context.editorContext || !context.editorContext->assetDatabase) {
		return nullptr;
	}
	// 空IDは描画に使う既定Materialへ戻す
	if (!materialID) {
		materialID = defaultMaterialID;
	}
	AssetDatabase& database = *context.editorContext->assetDatabase;
	auto lifetime = database.GetCacheLifetime();
	uint64_t revision = database.GetStructureRevision();
	uint64_t contentRevision = database.GetContentRevision(materialID);
	std::owner_less<void> less;
	bool sameDatabase = !cachedDatabaseLifetime_.expired() &&
		!less(cachedDatabaseLifetime_, lifetime) && !less(lifetime, cachedDatabaseLifetime_);
	// 同じMaterialの保存とProject切替も読込へ反映する
	if (!cachedMaterialValid_ || cachedMaterialID_ != materialID || !sameDatabase ||
		cachedDatabaseRevision_ != revision || cachedMaterialRevision_ != contentRevision) {

		cachedMaterialValid_ = false;
		cachedMaterialID_ = materialID;
		cachedDatabaseLifetime_ = lifetime;
		cachedDatabaseRevision_ = revision;
		cachedMaterialRevision_ = contentRevision;
		cachedMaterial_ = MaterialAsset{};
		const std::filesystem::path path = database.ResolveFullPath(materialID);
		if (!path.empty()) {

			// 読込失敗を空のMaterialとして扱わない
			nlohmann::json data;
			cachedMaterialValid_ = JsonFile::TryLoad(path, data) && FromJson(data, cachedMaterial_);
		}
	}
	if (!cachedMaterialValid_) {
		return nullptr;
	}
	return context.renderPipeline->FindMaterialDrawReflection(cachedMaterial_);
}

Engine::MaterialParameterValue Engine::MaterialReflectionCache::ResolveValue(
	const MaterialInstanceParameters& parameters, const ShaderConstantBufferVariable& var) const {

	// InstanceのIDと用途を優先する
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
	// Materialの既定値へ戻す
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
	return MaterialParameterDefaults::BuildValue(var);
}
