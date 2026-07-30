#include "FillMeshRendererInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Rendering/Materials/DefaultMaterialSettings.h>

//============================================================================
//	FillMeshRendererInspectorDrawer classMethods
//============================================================================
void Engine::FillMeshRendererInspectorDrawer::DrawFields(const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();

	// マテリアルと色
	DrawField(anyItemActive, [&]() {
		AssetEditSetting setting{};
		setting.defaultAssetID = DefaultMaterialSettings::GetInstance().GetFillMeshOrBuiltin();
		return MyGUI::AssetReferenceField("マテリアル", draft.material,
			context.editorContext->assetDatabase, { AssetType::Material }, setting);
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::ColorEdit("色", draft.color);
		});

	// 面を構成するXZ平面の点列
	for (size_t i = 0; i < positionDraft_.size(); ++i) {

		ImGui::PushID(static_cast<int>(i));
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragVector3("点", positionDraft_[i], { .dragSpeed = 0.1f });
			});
		ImGui::PopID();
	}
	DrawField(anyItemActive, [&]() {

		ValueEditResult result{};
		if (ImGui::Button("点を追加")) {
			positionDraft_.push_back(Vector3::AnyInit(0.0f));
			result.valueChanged = true;
			result.editFinished = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("末尾を削除") && !positionDraft_.empty()) {
			positionDraft_.pop_back();
			result.valueChanged = true;
			result.editFinished = true;
		}
		return result;
		});

	// 押すと次フレームにメッシュを構築する
	DrawField(anyItemActive, [&]() {

		ValueEditResult result{};
		if (ImGui::Button("メッシュ構築")) {
			draft.buildMesh = true;
			result.valueChanged = true;
			result.editFinished = true;
		}
		return result;
		});

	// 描画パラメータ
	InspectorDrawerCommon::DrawCommonRenderFields(
		[&](auto&& f) { DrawField(anyItemActive, std::forward<decltype(f)>(f)); },
		draft.layer, draft.order, draft.visible, draft.blendMode, draft.queue);
	DrawField(anyItemActive, [&]() {
		int32_t mask =
			static_cast<int32_t>(
				draft.renderingLayerMask);
		ValueEditResult result =
			MyGUI::DragInt(
				"描画対象マスク", mask,
				{ .minValue = 0,
				  .maxValue = static_cast<int32_t>(
					  kRenderingLayerMaskBits) });
		if (result.valueChanged) {
			draft.renderingLayerMask =
				static_cast<uint32_t>(mask);
		}
		return result;
		});
}

void Engine::FillMeshRendererInspectorDrawer::OnSyncDraftFromWorld(
	ECSWorld& world, const Entity& entity,
	[[maybe_unused]] const FillMeshRendererComponent& component) {

	const std::span<const FillMeshPosition> positions =
		GetFillMeshPositions(world, entity);
	positionDraft_.clear();
	positionDraft_.reserve(positions.size());
	for (const FillMeshPosition& position : positions) {
		positionDraft_.emplace_back(position.value);
	}
}

void Engine::FillMeshRendererInspectorDrawer::SerializeDraft(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	const FillMeshRendererComponent& component, nlohmann::json& out) const {

	std::vector<FillMeshPosition> positions{};
	positions.reserve(positionDraft_.size());
	for (const Vector3& position : positionDraft_) {
		positions.emplace_back(FillMeshPosition{ .value = position });
	}
	SerializeFillMeshRenderer(component, positions, out);
}

void Engine::FillMeshRendererInspectorDrawer::ApplyPreview(
	ECSWorld& world, const Entity& entity,
	const FillMeshRendererComponent& previewComponent) {

	if (!world.IsAlive(entity) ||
		!world.HasComponent<FillMeshRendererComponent>(entity)) {
		return;
	}
	world.GetComponent<FillMeshRendererComponent>(entity) = previewComponent;
	SetFillMeshPositions(world, entity, positionDraft_);
	world.MarkComponentModified<FillMeshRendererComponent>(entity);
}
