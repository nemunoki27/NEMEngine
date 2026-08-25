#include "SpriteRendererInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Rendering/Materials/DefaultMaterialSettings.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>

//============================================================================
//	SpriteRendererInspectorDrawer internal
//============================================================================
namespace {

	const std::string kBaseColorTextureParameter(
		Engine::MaterialParameterNames::BaseColorTexture);

	// 設定中テクスチャの実サイズを取得する、未ロードや未設定ならfalse
	bool TryResolveTextureSize(const Engine::EditorPanelContext& context,
		Engine::AssetID textureID, Engine::Vector2& outSize) {

		if (!context.graphicsCore || !context.editorContext) {
			return false;
		}
		return Engine::RuntimeTextureResolver::TryResolveSize(
			*context.graphicsCore, context.editorContext->assetDatabase, textureID, outSize);
	}
}

//============================================================================
//	SpriteRendererInspectorDrawer classMethods
//============================================================================
void Engine::SpriteRendererInspectorDrawer::DrawFields(const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	// ドラフトコンポーネントを参照
	auto& draft = GetDraft();

	//============================================================================
	//	アセットファイル
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			AssetEditSetting setting{};
				setting.defaultAssetID = DefaultMaterialSettings::GetInstance().GetSpriteOrBuiltin();
				return MyGUI::AssetReferenceField("マテリアル", draft.material,
					context.editorContext->assetDatabase, { AssetType::Material }, setting);
			});
	}
	//============================================================================
	//	スプライト見た目パラメータ
	//============================================================================
	{
		const AssetID defaultMaterialID = DefaultMaterialSettings::GetInstance().GetSpriteOrBuiltin();
		const AssetID baseColorTexture = materialParameterDrawer_.ResolveTextureParameter(context,
			draft.material, defaultMaterialID, draft.materialInstance, kBaseColorTextureParameter);

		DrawField(anyItemActive, [&]() {
			return MyGUI::DragVector2("サイズ", draft.size,
				{ .dragSpeed = 0.1f, .minValue = 0.0f, .maxValue = 100000.0f });
			});

		// サイズを設定中テクスチャの実サイズへ合わせる
		DrawField(anyItemActive, [&]() {

			ValueEditResult result{};
			const float fullWidth = ImGui::GetContentRegionAvail().x;
			const float buttonWidth = fullWidth * 0.5f;
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (fullWidth - buttonWidth) * 0.5f);
			ImGui::BeginDisabled(!baseColorTexture);
			if (ImGui::Button("サイズをテクスチャに合わせる", ImVec2(buttonWidth, 0.0f))) {

				Vector2 textureSize{};
				if (TryResolveTextureSize(context, baseColorTexture, textureSize)) {

					draft.size = textureSize;
					result.valueChanged = true;
					result.editFinished = true;
				}
			}
			ImGui::EndDisabled();
			return result;
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragVector2("ピボット", draft.pivot,
				{ .dragSpeed = 0.01f, .minValue = -1.0f, .maxValue = 1.0f });
			});
	}

	//============================================================================
	//	スプライト描画パラメータ
	//============================================================================
	// 描画設定
	{
		const AssetID defaultMaterialID = DefaultMaterialSettings::GetInstance().GetSpriteOrBuiltin();
		const AssetID previousTexture = materialParameterDrawer_.ResolveTextureParameter(context,
			draft.material, defaultMaterialID, draft.materialInstance, kBaseColorTextureParameter);

		InspectorDrawerCommon::DrawCommonRenderFields(
			[&](auto&& f) { DrawField(anyItemActive, std::forward<decltype(f)>(f)); },
			draft.layer, draft.order, draft.visible, draft.blendMode, draft.queue,
			&draft.renderingLayerMask);
		// シェーダーパラメータ
		materialParameterDrawer_.Draw(context, draft.material, defaultMaterialID, draft.materialInstance,
			[&](auto&& drawField) { DrawField(anyItemActive, std::forward<decltype(drawField)>(drawField)); });
	}
}
