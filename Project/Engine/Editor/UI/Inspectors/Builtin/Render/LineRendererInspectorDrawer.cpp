#include "LineRendererInspectorDrawer.h"
#include <Engine/Editor/UI/Common/SceneEntityReferenceFields.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Inspectors/Common/RendererRenderSettingsFields.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <algorithm>
#include <string>

#include <imgui.h>

//============================================================================
//	LineRendererInspectorDrawer internal
//============================================================================
namespace {

	// 親エンティティ参照欄、ヒエラルキーからドロップでlocalFileIDを設定しクリアで自身に戻す
	Engine::ValueEditResult DrawParentField(const char* label, Engine::ECSWorld& world, Engine::UUID& target) {

		return Engine::SceneEntityReferenceFields::DrawLocal(label, world, target);
	}
}

//============================================================================
//	LineRendererInspectorDrawer classMethods
//============================================================================
void Engine::LineRendererInspectorDrawer::DrawFields(const EditorPanelContext& context,
	ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	// ドラフトコンポーネントを参照
	auto& draft = GetDraft();

	//============================================================================
	//	マテリアル
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			AssetEditSetting setting{};
				setting.defaultAssetID = InspectorDrawerCommon::ResolveDefaultMaterial(context, DefaultMaterialSlot::Line);
				return MyGUI::AssetReferenceField("マテリアル", draft.material,
					context.editorContext->assetDatabase, { AssetType::Material }, setting);
			});
	}
	PushEditResult(DrawRendererRenderSettingsFields(draft.renderSettings), anyItemActive);
	//============================================================================
	//	ライン描画パラメータ
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("2D描画", draft.is2D);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("ワールド座標", draft.useWorldSpace);
			});
		// ワールド座標でないときは親に追従する、親の設定とスケール/回転の無視を出す
		if (!draft.useWorldSpace) {

			DrawField(anyItemActive, [&]() {
				return DrawParentField("親", world, draft.parentLocalFileID);
				});
			DrawField(anyItemActive, [&]() {
				return InspectorDrawerCommon::DrawCheckboxField("親のスケールを無視", draft.ignoreParentScale);
				});
			DrawField(anyItemActive, [&]() {
				return InspectorDrawerCommon::DrawCheckboxField("親の回転を無視", draft.ignoreParentRotation);
				});
		}
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("始点と終点を閉じる", draft.loop);
			});
		InspectorDrawerCommon::DrawCommonRenderFields(context,
			[&](auto&& f) { DrawField(anyItemActive, std::forward<decltype(f)>(f)); },
			draft.layer, draft.order, draft.blendMode, draft.queue);
	}
	//============================================================================
	//	点列
	//============================================================================
	{
		MyGUI::TextUInt64("点数", pointDraft_.size());

		// 末尾へ点を追加する、直前の点があればずらして置く
		DrawField(anyItemActive, [&]() {

			ValueEditResult result{};
			if (ImGui::Button("点を追加")) {

				LinePoint point{};
				if (!pointDraft_.empty()) {

					point = pointDraft_.back();
					point.position.x += 1.0f;
				}
				pointDraft_.emplace_back(point);
				result.valueChanged = true;
				result.editFinished = true;
			}
			return result;
			});

		// 各点の座標と色と太さを編集する
		for (size_t i = 0; i < pointDraft_.size(); ++i) {

			ImGui::PushID(static_cast<int>(i));
			LinePoint& point = pointDraft_[i];

			DrawField(anyItemActive, [&]() {
				return MyGUI::DragVector3("座標", point.position, { .dragSpeed = 0.05f });
				});
			DrawField(anyItemActive, [&]() {
				return MyGUI::ColorEdit("色", point.color);
				});
			DrawField(anyItemActive, [&]() {
				return MyGUI::DragFloat("太さ", point.thickness,
					{ .dragSpeed = 0.05f, .minValue = 0.0f, .maxValue = 1000.0f });
				});
			DrawField(anyItemActive, [&]() {

				ValueEditResult result{};
				if (ImGui::Button("この点を削除")) {

					pointDraft_.erase(pointDraft_.begin() + i);
					result.valueChanged = true;
					result.editFinished = true;
				}
				return result;
				});

			ImGui::PopID();

			// 削除でこのフレームの点列が縮んだら以降の描画は次フレームへ回す
			if (i >= pointDraft_.size()) {
				break;
			}
		}
	}
}

void Engine::LineRendererInspectorDrawer::OnSyncDraftFromWorld(
	ECSWorld& world, const Entity& entity,
	[[maybe_unused]] const LineRendererComponent& component) {

	const std::span<const LinePoint> points = GetLinePoints(world, entity);
	pointDraft_.assign(points.begin(), points.end());
}

void Engine::LineRendererInspectorDrawer::SerializeDraft(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	const LineRendererComponent& component, nlohmann::json& out) const {

	SerializeLineRenderer(component, pointDraft_, out);
}

void Engine::LineRendererInspectorDrawer::ApplyPreview(
	ECSWorld& world, const Entity& entity,
	const LineRendererComponent& previewComponent) {

	if (!world.IsAlive(entity) ||
		!world.HasComponent<LineRendererComponent>(entity)) {
		return;
	}
	world.GetComponent<LineRendererComponent>(entity) = previewComponent;
	SetLinePoints(world, entity, pointDraft_);
	world.MarkComponentModified<LineRendererComponent>(entity);
}
