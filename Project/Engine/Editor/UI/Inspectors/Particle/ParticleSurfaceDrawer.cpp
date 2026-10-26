#include "ParticleSurfaceDrawer.h"

//============================================================================
//	include
//============================================================================
#include "GUI/ParticleGUIHelpers.h"
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

bool Engine::ParticleSurfaceDrawer::Draw(const EditorPanelContext& context, ParticleSurfaceSettings& settings) {

	using ParticleGUI::MakeDragSetting;
	ImGui::SeparatorText("表面");
	bool changed = MyGUI::Checkbox("深度に沿ってフェード", settings.softParticles);
	if (settings.softParticles) {
		changed |= MyGUI::DragFloat("フェード距離", settings.softDistance, MakeDragSetting(0.0001f, 10000.0f)).valueChanged;
	}
	changed |= MyGUI::Checkbox("ライトを受ける", settings.lit);
	if (settings.lit) {
		changed |= MyGUI::Checkbox("影を受ける", settings.receiveShadows);
		changed |= MyGUI::Checkbox("環境光を受ける", settings.receiveIBL);
		AssetEditSetting edit;
		changed |= MyGUI::AssetReferenceField("法線テクスチャ", settings.normalTexture,
			context.editorContext->assetDatabase, {AssetType::Texture, AssetType::RenderTexture}, edit).valueChanged;
		changed |= MyGUI::DragFloat("法線の強さ", settings.normalScale, MakeDragSetting(0.0f, 10.0f)).valueChanged;
		changed |= MyGUI::DragFloat("メタリック", settings.metallic, MakeDragSetting(0.0f, 1.0f)).valueChanged;
		changed |= MyGUI::DragFloat("ラフネス", settings.roughness, MakeDragSetting(0.04f, 1.0f)).valueChanged;
	}
	return changed;
}
