#include "TextRendererInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Rendering/Materials/DefaultMaterialSettings.h>
#include <Engine/Editor/Assets/Importer/Font/MSDFFontGenerator.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

//============================================================================
//	TextRendererInspectorDrawer classMethods
//============================================================================
void Engine::TextRendererInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	// ドラフトコンポーネントを参照
	auto& draft = GetDraft();

	//============================================================================
	//	アセットファイル
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return MyGUI::AssetReferenceField("フォント", draft.font,
				context.editorContext->assetDatabase, { AssetType::Font });
			});
		// ドロップ直後はdraft.fontが.ttf/.otfを指すので、MSDFを生成して.font.jsonの参照へ寄せる
		ResolveFontSourceDrop(context);
		DrawField(anyItemActive, [&]() {
			AssetEditSetting setting{};
			setting.defaultAssetID = DefaultMaterialSettings::GetInstance().GetTextOrBuiltin();
			return MyGUI::AssetReferenceField("マテリアル", draft.material,
				context.editorContext->assetDatabase, { AssetType::Material }, setting);
			});
	}
	//============================================================================
	//	テキスト見た目パラメータ
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return MyGUI::InputText("テキスト", draft.text, { .multiLine = true });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("フォントサイズ", draft.fontSize,
				{ .dragSpeed = 0.1f, .minValue = 0.0f, .maxValue = 10000.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("文字間隔", draft.charSpacing,
				{ .dragSpeed = 0.01f, .minValue = -1000.0f, .maxValue = 1000.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragVector2("ピボット", draft.pivot,
				{ .dragSpeed = 0.01f, .minValue = -1.0f, .maxValue = 1.0f });
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("UVを文字ごとに適用", draft.uvPerCharacter);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawEnumComboField("描画次元", draft.dimension);
			});
		// ワールドスケールは3D描画でのみ意味を持つので2Dでは表示しない
		if (draft.dimension == Dimension::Type3D) {
			DrawField(anyItemActive, [&]() {
				return MyGUI::DragFloat("ワールドスケール", draft.worldScale,
					{ .dragSpeed = 0.001f, .minValue = 0.0001f, .maxValue = 1000.0f });
				});
		}
	}

	//============================================================================
	//	テキスト描画パラメータ
	//============================================================================
	{
		// 描画設定
		InspectorDrawerCommon::DrawCommonRenderFields(
			[&](auto&& f) { DrawField(anyItemActive, std::forward<decltype(f)>(f)); },
			draft.layer, draft.order, draft.visible, draft.blendMode, draft.queue);
		materialParameterDrawer_.Draw(context, draft.material,
			DefaultMaterialSettings::GetInstance().GetTextOrBuiltin(), draft.materialInstance,
			[&](auto&& drawField) { DrawField(anyItemActive, std::forward<decltype(drawField)>(drawField)); });
	}
	//============================================================================
	//	文字ごとのトランスフォーム
	//============================================================================
	{
		ImGui::SeparatorText("文字ごとのトランスフォーム");

		// 表示中のグリフ数に合わせて行を用意する、値を編集したときだけコミットされる
		// 非表示などでレイアウト未生成のときはサイズ0で潰さず保存済みデータを温存する
		const size_t glyphCount = GetTextLayoutGlyphs(world, entity).size();
		if (glyphCount == 0) {

			ImGui::TextDisabled("表示中の文字がありません");
		} else {

			if (charTransformDraft_.size() != glyphCount) {
				charTransformDraft_.resize(glyphCount);
			}
			for (size_t i = 0; i < charTransformDraft_.size(); ++i) {

				TextCharTransform& charTransform = charTransformDraft_[i];
				ImGui::PushID(static_cast<int>(i));
				if (ImGui::TreeNode("CharTransform", "文字 %zu", i)) {

					DrawField(anyItemActive, [&]() {
						return MyGUI::DragVector2("位置", charTransform.translation,
							{ .dragSpeed = 0.1f, .minValue = -100000.0f, .maxValue = 100000.0f });
						});
					DrawField(anyItemActive, [&]() {
						return MyGUI::DragFloat("回転", charTransform.rotation,
							{ .dragSpeed = 0.5f, .minValue = -100000.0f, .maxValue = 100000.0f });
						});
					DrawField(anyItemActive, [&]() {
						return MyGUI::DragVector2("スケール", charTransform.scale,
							{ .dragSpeed = 0.01f, .minValue = -100000.0f, .maxValue = 100000.0f });
						});
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
		}
	}
}

void Engine::TextRendererInspectorDrawer::OnSyncDraftFromWorld(
	ECSWorld& world, const Entity& entity,
	[[maybe_unused]] const TextRendererComponent& component) {

	const std::span<const TextCharTransform> transforms =
		GetTextCharTransforms(world, entity);
	charTransformDraft_.assign(transforms.begin(), transforms.end());
}

void Engine::TextRendererInspectorDrawer::SerializeDraft(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	const TextRendererComponent& component, nlohmann::json& out) const {

	SerializeTextRenderer(component, charTransformDraft_, out);
}

void Engine::TextRendererInspectorDrawer::ApplyPreview(
	ECSWorld& world, const Entity& entity,
	const TextRendererComponent& previewComponent) {

	if (!world.IsAlive(entity) ||
		!world.HasComponent<TextRendererComponent>(entity)) {
		return;
	}
	world.GetComponent<TextRendererComponent>(entity) = previewComponent;
	SetTextCharTransforms(world, entity, charTransformDraft_);
	InvalidateTextLayout(world, entity);
	world.MarkComponentModified<TextRendererComponent>(entity);
}

void Engine::TextRendererInspectorDrawer::ResolveFontSourceDrop(const EditorPanelContext& context) {

	auto& draft = GetDraft();

	AssetDatabase* database = context.editorContext ? context.editorContext->assetDatabase : nullptr;
	if (!database || !draft.font) {
		return;
	}

	// 参照先がソースフォント以外なら触らない、生成済みの.font.jsonはそのまま使う
	const AssetMeta* meta = database->Find(draft.font);
	if (!meta || !MSDFFontGenerator::IsFontSourceExtension(meta->assetPath)) {
		return;
	}

	// 未生成なら隣にMSDFを作る、成否いずれでもdraft.fontはソース以外へ抜けるので毎フレーム再入はしない
	const std::filesystem::path sourcePath = database->ResolveFullPath(draft.font);
	const MSDFFontGenerator::Result result = MSDFFontGenerator::EnsureGenerated(*database, sourcePath, false);
	if (result.success) {

		draft.font = result.fontAssetID;
	} else {

		Logger::Output(LogType::Engine, spdlog::level::warn,
			"[TextRendererInspector] Font生成に失敗しました {}", result.message);
		draft.font = AssetID{};
	}
	RequestCommit();
}
