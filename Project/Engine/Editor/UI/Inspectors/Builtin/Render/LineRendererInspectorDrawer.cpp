#include "LineRendererInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Rendering/Materials/DefaultMaterialSettings.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>

// imgui
#include <imgui.h>

// c++
#include <algorithm>
#include <string>

//============================================================================
//	LineRendererInspectorDrawer internal
//============================================================================
namespace {

	// 親localFileIDから表示名を作る、未設定はなし扱い
	std::string MakeParentLabel(Engine::ECSWorld& world, Engine::UUID target) {

		if (!target) {
			return "自身のTransform";
		}
		const Engine::Entity entity = Engine::SceneObjectUtility::FindByLocalFileID(world, target);
		if (!entity.IsValid() || !world.IsAlive(entity)) {
			return "不明 : " + Engine::ToString(target);
		}
		if (world.HasComponent<Engine::NameComponent>(entity)) {
			return world.GetComponent<Engine::NameComponent>(entity).name;
		}
		return Engine::ToString(target);
	}

	// 親エンティティ参照欄、ヒエラルキーからドロップでlocalFileIDを設定しクリアで自身に戻す
	Engine::ValueEditResult DrawParentField(const char* label, Engine::ECSWorld& world, Engine::UUID& target) {

		using namespace Engine;
		ValueEditResult result{};
		if (!MyGUI::BeginPropertyRow(label)) {
			return result;
		}

		const float clearWidth = 56.0f;
		const std::string buttonLabel = MakeParentLabel(world, target);
		ImGui::Button(buttonLabel.c_str(), ImVec2((std::max)(0.0f, ImGui::GetContentRegionAvail().x - clearWidth), 0.0f));
		result.anyItemActive |= ImGui::IsItemActive();

		if (ImGui::BeginDragDropTarget()) {

			const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IEditorPanel::kHierarchyDragDropPayloadType);
			if (payload && payload->DataSize == sizeof(Engine::UUID)) {

				// ヒエラルキーのpayloadはrecords_のuuidなので、Edit/Playで安定するlocalFileIDへ変換する
				const Engine::UUID droppedUUID = *static_cast<const Engine::UUID*>(payload->Data);
				const Entity dropped = world.FindByUUID(droppedUUID);
				Engine::UUID localFileID{};
				if (dropped.IsValid() && world.HasComponent<SceneObjectComponent>(dropped)) {
					localFileID = world.GetComponent<SceneObjectComponent>(dropped).localFileID;
				}
				if (localFileID && target != localFileID) {

					target = localFileID;
					result.valueChanged = true;
					result.editFinished = true;
				}
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::SameLine();
		if (ImGui::Button("クリア", ImVec2(clearWidth, 0.0f))) {

			target = Engine::UUID{};
			result.valueChanged = true;
			result.editFinished = true;
		}
		MyGUI::EndPropertyRow();
		return result;
	}
}

//============================================================================
//	LineRendererInspectorDrawer classMethods
//============================================================================
void Engine::LineRendererInspectorDrawer::DrawFields(const EditorPanelContext& context,
	ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	// ドラフトコンポーネントを参照
	auto& draft = GetDraft();
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("表示", draft.visible);
		});

	//============================================================================
	//	マテリアル
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			AssetEditSetting setting{};
				setting.defaultAssetID = DefaultMaterialSettings::GetInstance().GetLineOrBuiltin();
				return MyGUI::AssetReferenceField("マテリアル", draft.material,
					context.editorContext->assetDatabase, { AssetType::Material }, setting);
			});
	}
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
		InspectorDrawerCommon::DrawCommonRenderFields(
			[&](auto&& f) { DrawField(anyItemActive, std::forward<decltype(f)>(f)); },
			draft.layer, draft.order, draft.blendMode, draft.queue,
			&draft.renderingLayerMask);
	}
	//============================================================================
	//	点列
	//============================================================================
	{
		ImGui::Text("点数 %d", static_cast<int>(pointDraft_.size()));

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
