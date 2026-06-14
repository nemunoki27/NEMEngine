#include "SpriteRendererInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Rendering/Textures/GPUTextureResource.h>

//============================================================================
//	SpriteRendererInspectorDrawer internal
//============================================================================
namespace {

	// 設定中テクスチャの実サイズを取得する、未ロードや未設定ならfalse
	bool TryResolveTextureSize(const Engine::EditorPanelContext& context,
		Engine::AssetID textureID, Engine::Vector2& outSize) {

		if (!textureID || !context.graphicsCore ||
			!context.editorContext || !context.editorContext->assetDatabase) {
			return false;
		}
		const Engine::GPUTextureResource* texture = Engine::RuntimeTextureResolver::Resolve(
			*context.graphicsCore, context.editorContext->assetDatabase, textureID, false);
		if (!texture || !texture->valid || !texture->resource) {
			return false;
		}
		const D3D12_RESOURCE_DESC desc = texture->resource->GetDesc();
		outSize = Engine::Vector2(static_cast<float>(desc.Width), static_cast<float>(desc.Height));
		return true;
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
				setting.graphicsCore = context.graphicsCore;
				return MyGUI::AssetReferenceField("テクスチャ", draft.texture,
					context.editorContext->assetDatabase, { AssetType::Texture }, setting);
			});
		DrawField(anyItemActive, [&]() {
			AssetEditSetting setting{};
				setting.defaultAssetID = BuiltinAssets::Materials::DefaultSprite;
				return MyGUI::AssetReferenceField("マテリアル", draft.material,
					context.editorContext->assetDatabase, { AssetType::Material }, setting);
			});
	}
	//============================================================================
	//	スプライト見た目パラメータ
	//============================================================================
	{
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
			ImGui::BeginDisabled(!draft.texture);
			if (ImGui::Button("サイズをテクスチャに合わせる", ImVec2(buttonWidth, 0.0f))) {

				Vector2 textureSize{};
				if (TryResolveTextureSize(context, draft.texture, textureSize)) {

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
		DrawField(anyItemActive, [&]() {
			return MyGUI::ColorEdit("色", draft.color);
			});
	}
	//============================================================================
	//	スプライト描画パラメータ
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragInt("レイヤー", draft.layer);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragInt("描画順", draft.order);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("表示", draft.visible);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawEnumComboField("ブレンドモード", draft.blendMode);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawEnumComboField("キュー", draft.queue);
			});
	}
}
