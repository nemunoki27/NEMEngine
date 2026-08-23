#include "CollisionInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Physics/Collision/CollisionSettings.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

// imgui
#include <imgui.h>

//============================================================================
//	CollisionInspectorDrawer classMethods
//============================================================================
void Engine::CollisionInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	CollisionSettings::GetInstance().EnsureLoaded();

	// CollisionComponent全体の設定
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("静的", draft.isStatic);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("押し戻し", draft.enablePushback);
		});
	DrawField(anyItemActive, [&]() {
		return DrawTypeMaskField(draft);
		});

	ImGui::Indent();

	if (!MyGUI::CollapsingHeader("形状一覧")) {
		ImGui::Unindent();
		return;
	}

	// 登録されている衝突形状を描画する
	int32_t removeIndex = -1;
	for (uint32_t i = 0; i < static_cast<uint32_t>(shapeDraft_.size()); ++i) {

		ImGui::PushID(static_cast<int32_t>(i));
		CollisionShape& shape = shapeDraft_[i];
		if (ImGui::TreeNodeEx("形状", ImGuiTreeNodeFlags_DefaultOpen, "形状 %u : %s", i,
			EnumAdapter<ColliderShapeType>::ToString(shape.type))) {

			PushEditResult(DrawShapeField(shape, i), anyItemActive);
			if (ImGui::Button("形状を削除", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				removeIndex = static_cast<int32_t>(i);
			}
			ImGui::TreePop();
		}
		ImGui::Separator();
		ImGui::PopID();
	}

	if (0 <= removeIndex && removeIndex < static_cast<int32_t>(shapeDraft_.size())) {
		shapeDraft_.erase(shapeDraft_.begin() + removeIndex);
		if (shapeDraft_.empty()) {
			shapeDraft_.push_back(CollisionShape{});
		}
		RequestCommit();
	}

	if (ImGui::Button("形状を追加", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		shapeDraft_.push_back(CollisionShape{});
		RequestCommit();
	}

	ImGui::Unindent();
}

void Engine::CollisionInspectorDrawer::OnSyncDraftFromWorld(
	ECSWorld& world, const Entity& entity,
	[[maybe_unused]] const CollisionComponent& component) {

	const std::span<const CollisionShape> shapes =
		GetCollisionShapes(world, entity);
	shapeDraft_.assign(shapes.begin(), shapes.end());
	if (shapeDraft_.empty()) {
		shapeDraft_.emplace_back();
	}
}

void Engine::CollisionInspectorDrawer::SerializeDraft(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	const CollisionComponent& component, nlohmann::json& out) const {

	SerializeCollisionDraft(component, shapeDraft_, out);
}

void Engine::CollisionInspectorDrawer::ApplyPreview(ECSWorld& world,
	const Entity& entity, const CollisionComponent& previewComponent) {

	if (!world.IsAlive(entity)) {
		return;
	}
	CollisionComponent* component =
		world.TryGetComponent<CollisionComponent>(entity);
	if (!component) {
		return;
	}

	*component = previewComponent;
	// 形状は独立Bufferのため、設定プレビュー後に編集用配列だけを書き戻す
	SetCollisionShapes(world, entity, shapeDraft_);
	world.MarkComponentModified<CollisionComponent>(entity);
}

Engine::ValueEditResult Engine::CollisionInspectorDrawer::DrawTypeMaskField(CollisionComponent& component) {

	ValueEditResult result{};
	if (!MyGUI::BeginPropertyRow("衝突タイプ")) {
		return result;
	}

	CollisionSettings& settings = CollisionSettings::GetInstance();
	const auto& types = settings.GetTypes();

	// 付与済みのタイプを1行ずつ縦に並べ、各行で解除できるようにする
	int32_t removeIndex = -1;
	uint32_t assignedCount = 0;
	for (uint32_t i = 0; i < static_cast<uint32_t>(types.size()); ++i) {

		if (!HasCollisionType(component.typeMask, i)) {
			continue;
		}
		++assignedCount;

		ImGui::PushID(static_cast<int32_t>(i));
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(types[i].name.c_str());

		// 解除ボタンを行末へ寄せる
		ImGui::SameLine();
		const float buttonWidth = 48.0f;
		const float pad = ImGui::GetContentRegionAvail().x - buttonWidth;
		if (0.0f < pad) {
			ImGui::Dummy(ImVec2(pad, 0.0f));
			ImGui::SameLine();
		}
		if (ImGui::SmallButton("解除")) {
			removeIndex = static_cast<int32_t>(i);
		}
		result.anyItemActive |= ImGui::IsItemActive();
		ImGui::PopID();
	}
	if (assignedCount == 0) {
		ImGui::TextDisabled("未設定");
	}

	// 解除指定があればビットを下ろす
	if (0 <= removeIndex) {
		component.typeMask &= ~MakeCollisionTypeBit(static_cast<uint32_t>(removeIndex));
		result.valueChanged = true;
		result.editFinished = true;
	}

	// まだ付与していないタイプをコンボから選んで追加する、選択した時点で付与する
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
	if (ImGui::BeginCombo("##AddCollisionType", "＋ タイプを追加", ImGuiComboFlags_HeightLargest)) {
		for (uint32_t i = 0; i < static_cast<uint32_t>(types.size()); ++i) {

			if (HasCollisionType(component.typeMask, i)) {
				continue;
			}
			if (ImGui::Selectable(types[i].name.c_str())) {
				component.typeMask |= MakeCollisionTypeBit(i);
				result.valueChanged = true;
				result.editFinished = true;
			}
		}
		ImGui::EndCombo();
	}
	result.anyItemActive |= ImGui::IsItemActive();
	MyGUI::EndPropertyRow();
	return result;
}

Engine::ValueEditResult Engine::CollisionInspectorDrawer::DrawShapeTypeField(ColliderShapeType& type) {

	ValueEditResult result{};
	if (!MyGUI::BeginPropertyRow("タイプ")) {
		return result;
	}

	if (EnumAdapter<ColliderShapeType>::Combo("##Value", &type)) {
		result.valueChanged = true;
		result.editFinished = true;
	}
	result.anyItemActive = ImGui::IsItemActive();
	MyGUI::EndPropertyRow();
	return result;
}

Engine::ValueEditResult Engine::CollisionInspectorDrawer::DrawCapsuleAxisField(
	CapsuleAxis& axis, bool is2D) {

	ValueEditResult result{};
	if (!MyGUI::BeginPropertyRow("方向")) {
		return result;
	}

	if (is2D) {

		const bool horizontal = axis == CapsuleAxis::X;
		if (ImGui::BeginCombo("##Value", horizontal ? "横" : "縦")) {
			if (ImGui::Selectable("横", horizontal)) {
				axis = CapsuleAxis::X;
				result.valueChanged = true;
				result.editFinished = true;
			}
			if (ImGui::Selectable("縦", !horizontal)) {
				axis = CapsuleAxis::Y;
				result.valueChanged = true;
				result.editFinished = true;
			}
			ImGui::EndCombo();
		}
	} else if (EnumAdapter<CapsuleAxis>::Combo("##Value", &axis)) {
		result.valueChanged = true;
		result.editFinished = true;
	}
	result.anyItemActive = ImGui::IsItemActive();
	MyGUI::EndPropertyRow();
	return result;
}

Engine::ValueEditResult Engine::CollisionInspectorDrawer::DrawShapeField(
	CollisionShape& shape, [[maybe_unused]] uint32_t index) {

	ValueEditResult result{};
	auto accumulate = [&](const ValueEditResult& item) {
		result.valueChanged |= item.valueChanged;
		result.anyItemActive |= item.anyItemActive;
		result.editFinished |= item.editFinished;
		};

	accumulate(DrawShapeTypeField(shape.type));
	accumulate(InspectorDrawerCommon::DrawCheckboxField("有効", shape.enabled));
	accumulate(InspectorDrawerCommon::DrawCheckboxField("コールバックのみ", shape.isTrigger));
	accumulate(InspectorDrawerCommon::DrawCheckboxField("トランスフォーム回転使用", shape.useTransformRotation));
	if (shape.type == ColliderShapeType::Capsule2D) {

		Vector2 offset(shape.offset.x, shape.offset.y);
		const ValueEditResult offsetResult =
			MyGUI::DragVector2("オフセット", offset);
		if (offsetResult.valueChanged) {
			shape.offset.x = offset.x;
			shape.offset.y = offset.y;
		}
		accumulate(offsetResult);

		const ValueEditResult rotationResult = MyGUI::DragFloat(
			"回転", shape.rotationDegrees.z, { .dragSpeed = 0.1f });
		accumulate(rotationResult);
		if (shape.offset.z != 0.0f ||
			shape.rotationDegrees.x != 0.0f ||
			shape.rotationDegrees.y != 0.0f) {

			shape.offset.z = 0.0f;
			shape.rotationDegrees.x = 0.0f;
			shape.rotationDegrees.y = 0.0f;
			result.valueChanged = true;
			result.editFinished = true;
		}
	} else {

		accumulate(MyGUI::DragVector3("オフセット", shape.offset));
		accumulate(MyGUI::DragVector3(
			"回転", shape.rotationDegrees, { .dragSpeed = 0.1f }));
	}

	// 形状タイプに必要なパラメータだけを表示する
	if (shape.type == ColliderShapeType::Circle2D ||
		shape.type == ColliderShapeType::Sphere3D ||
		shape.type == ColliderShapeType::Capsule3D) {
		accumulate(MyGUI::DragFloat("半径", shape.radius, { .dragSpeed = 0.1f, .minValue = 0.0f }));
	}
	if (shape.type == ColliderShapeType::Quad2D) {
		accumulate(InspectorDrawerCommon::DrawCheckboxField("回転四角", shape.rotatedQuad));
		accumulate(MyGUI::DragVector2("半サイズ", shape.halfSize2D, { .dragSpeed = 0.1f, .minValue = 0.0f }));
	}
	if (shape.type == ColliderShapeType::AABB3D || shape.type == ColliderShapeType::OBB3D) {
		accumulate(MyGUI::DragVector3("半サイズ", shape.halfExtents3D, { .dragSpeed = 0.01f, .minValue = 0.0f }));
	}
	if (shape.type == ColliderShapeType::Capsule2D) {
		accumulate(MyGUI::DragVector2(
			"サイズ", shape.capsuleSize2D, { .dragSpeed = 0.1f, .minValue = 0.0f }));
		accumulate(DrawCapsuleAxisField(shape.capsuleAxis, true));
	}
	if (shape.type == ColliderShapeType::Capsule3D) {
		accumulate(MyGUI::DragFloat(
			"高さ", shape.capsuleHeight, { .dragSpeed = 0.1f, .minValue = 0.0f }));
		accumulate(DrawCapsuleAxisField(shape.capsuleAxis, false));
	}
	return result;
}
