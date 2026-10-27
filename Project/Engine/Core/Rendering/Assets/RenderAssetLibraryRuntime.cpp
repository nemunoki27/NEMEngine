#include "RenderAssetLibrary.h"

//============================================================================
//	RenderAssetLibrary runtimeMethods
//============================================================================
void Engine::RenderAssetLibrary::SetRuntimeMaterial(MaterialAsset material) {

	// 最初の変更前のEditor用データを保持する
	if (!runtimeMaterialBaselines_.contains(material.guid)) {
		if (const auto* baseline = LoadMaterial(material.guid)) runtimeMaterialBaselines_.emplace(material.guid, *baseline);
	}
	RegisterDerivedMaterial(std::move(material));
}

void Engine::RenderAssetLibrary::SetRuntimeRenderTexture(RenderTextureAsset texture) {

	if (!runtimeRenderTextureBaselines_.contains(texture.guid)) {
		if (const auto* baseline = LoadRenderTexture(texture.guid)) runtimeRenderTextureBaselines_.emplace(texture.guid, *baseline);
	}
	renderTextureCache_.insert_or_assign(texture.guid, std::move(texture));
}

const Engine::MaterialAsset* Engine::RenderAssetLibrary::GetRuntimeMaterialBaseline(AssetID assetID) const {

	const auto found = runtimeMaterialBaselines_.find(assetID);
	return found == runtimeMaterialBaselines_.end() ? nullptr : &found->second;
}

void Engine::RenderAssetLibrary::ResetRuntimeAssets() {

	// GPU側の再生成は既存の世代判定と回収窓口へ任せる
	for (auto& [id, material] : runtimeMaterialBaselines_) RegisterDerivedMaterial(std::move(material));
	for (auto& [id, texture] : runtimeRenderTextureBaselines_) renderTextureCache_.insert_or_assign(id, std::move(texture));
	runtimeMaterialBaselines_.clear();
	runtimeRenderTextureBaselines_.clear();
}
