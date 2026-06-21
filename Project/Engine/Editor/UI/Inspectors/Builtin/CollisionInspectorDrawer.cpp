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

	if (!MyGUI::CollapsingHeader("Shapes")) {
		return;
	}

	// 登録されている衝突形状を描画する
	int32_t removeIndex = -1;
	for (uint32_t i = 0; i < static_cast<uint32_t>(draft.shapes.size()); ++i) {

		ImGui::PushID(static_cast<int32_t>(i));
		CollisionShape& shape = draft.shapes[i];
		if (ImGui::TreeNodeEx("Shape", ImGuiTreeNodeFlags_DefaultOpen, "Shape %u : %s", i,
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

	if (0 <= removeIndex && removeIndex < static_cast<int32_t>(draft.shapes.size())) {
		draft.shapes.erase(draft.shapes.begin() + removeIndex);
		if (draft.shapes.empty()) {
			draft.shapes.push_back(CollisionShape{});
		}
		RequestCommit();
	}

	if (ImGui::Button("形状を追加", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		draft.shapes.push_back(CollisionShape{});
		RequestCommit();
	}

	ImGui::Unindent();
}

Engine::ValueEditResult Engine::CollisionInspectorDrawer::DrawTypeMaskField(CollisionComponent& component) {

	ValueEditResult result{};
	if (!MyGUI::BeginPropertyRow("衝突タイプ")) {
		return result;
	}

	CollisionSettings& settings = CollisionSettings::GetInstance();
	const auto& types = settings.GetTypes();

	// 付与済みのタイプを1行ずつ縦に並べ、各行で解除できるようにする
	// タイプ数が増えても横にはみ出さないよう、追加形式のリストにする
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

Engine::ValueEditResult Engine::CollisionInspectorDrawer::DrawShapeField(CollisionShape& shape, [[maybe_unused]] uint32_t index) {

	ValueEditResult result{};
	auto accumulate = [&](const ValueEditResult& item) {
		result.valueChanged |= item.valueChanged;
		result.anyItemActive |= item.anyItemActive;
		result.editFinished |= item.editFinished;
		};

	accumulate(DrawShapeTypeField(shape.type));
	accumulate(InspectorDrawerCommon::DrawCheckboxField("有効", shape.enabled));
	accumulate(InspectorDrawerCommon::DrawCheckboxField("トリガー", shape.isTrigger));
	accumulate(InspectorDrawerCommon::DrawCheckboxField("変換回転", shape.useTransformRotation));
	accumulate(MyGUI::DragVector3("オフセット", shape.offset));
	accumulate(MyGUI::DragVector3("回転", shape.rotationDegrees, { .dragSpeed = 0.1f }));

	// 形状タイプに必要なパラメータだけを表示する
	if (shape.type == ColliderShapeType::Circle2D || shape.type == ColliderShapeType::Sphere3D) {
		accumulate(MyGUI::DragFloat("半径", shape.radius, { .dragSpeed = 0.01f, .minValue = 0.0f }));
	}
	if (shape.type == ColliderShapeType::Quad2D) {
		accumulate(InspectorDrawerCommon::DrawCheckboxField("回転四角", shape.rotatedQuad));
		accumulate(MyGUI::DragVector2("半サイズ", shape.halfSize2D, { .dragSpeed = 0.01f, .minValue = 0.0f }));
	}
	if (shape.type == ColliderShapeType::AABB3D || shape.type == ColliderShapeType::OBB3D) {
		accumulate(MyGUI::DragVector3("半径寸法", shape.halfExtents3D, { .dragSpeed = 0.01f, .minValue = 0.0f }));
	}
	return result;
}
