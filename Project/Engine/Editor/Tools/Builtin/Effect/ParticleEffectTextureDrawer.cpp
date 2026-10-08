#include "ParticleEffectTextureDrawer.h"

//============================================================================
//	include
//============================================================================
#include "ParticleEffectMaterialResolver.h"
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

bool Engine::ParticleEffectTextureDrawer::Draw(const EditorToolContext& context,
	ParticlePhaseMaterialSettings& settings, AssetID materialID) {

	using namespace ParticleEffectMaterialResolver;
	AssetDatabase* database = context.toolContext.assetDatabase;
	AssetEditSetting editSetting{};
	bool changed = MyGUI::AssetReferenceField("ベースカラーテクスチャ", settings.baseColorTexture,
		database, { AssetType::Texture, AssetType::RenderTexture }, editSetting).valueChanged;
	const auto material = LoadMaterialAsset(context, materialID);
	if (!material) {
		ImGui::TextDisabled("マテリアルを解決できません");
		return changed;
	}
	const ShaderReflectionInfo* reflection = FindParticleMaterialReflection(context, *material);
	if (!reflection) {
		ImGui::TextDisabled("シェーダーリフレクションを取得できません");
		return changed;
	}
	// Shaderが公開するTextureだけを名前で対応付ける
	for (const ShaderResourceBinding& texture : CollectMaterialTextures(*reflection)) {
		ImGui::PushID(texture.name.c_str());
		AssetID textureID{};
		if (auto found = settings.textureOverrides.find(texture.name); found != settings.textureOverrides.end()) {
			textureID = found->second;
		}
		if (MyGUI::AssetReferenceField(texture.name.c_str(), textureID, database,
			{ AssetType::Texture, AssetType::RenderTexture }, editSetting).valueChanged) {
			// 空の入力はoverrideを解除し、Materialの値へ戻す
			if (textureID) { settings.textureOverrides[texture.name] = textureID; }
			else { settings.textureOverrides.erase(texture.name); }
			changed = true;
		}
		ImGui::PopID();
	}
	return changed;
}
