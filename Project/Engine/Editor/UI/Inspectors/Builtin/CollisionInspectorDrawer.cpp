#include "CollisionInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Physics/Collision/CollisionSettings.h>
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
		if (ImGui::TreeNodeEx("Shape", ImGuiTreeNodeFlags_DefaultOpen, "Shape %u : %s", i, ToString(shape.type))) {

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

	// CollisionManagerで作成したタイプを複数選択できるようにする
	for (uint32_t i = 0; i < static_cast<uint32_t>(types.size()); ++i) {

		if (i != 0) {
			ImGui::SameLine();
		}
		bool enabled = HasCollisionType(component.typeMask, i);
		if (ImGui::Checkbox(types[i].name.c_str(), &enabled)) {
			if (enabled) {
				component.typeMask |= MakeCollisionTypeBit(i);
			} else {
				component.typeMask &= ~MakeCollisionTypeBit(i);
			}
			result.valueChanged = true;
			result.editFinished = true;
		}
		result.anyItemActive |= ImGui::IsItemActive();
	}
	MyGUI::EndPropertyRow();
	return result;
}

Engine::ValueEditResult Engine::CollisionInspectorDrawer::DrawShapeTypeField(ColliderShapeType& type) {

	ValueEditResult result{};
	if (!MyGUI::BeginPropertyRow("タイプ")) {
		return result;
	}

	constexpr ColliderShapeType kTypes[] = {
		ColliderShapeType::Circle2D,
		ColliderShapeType::Quad2D,
		ColliderShapeType::Sphere3D,
		ColliderShapeType::AABB3D,
		ColliderShapeType::OBB3D,
	};

	const char* current = ToString(type);
	if (ImGui::BeginCombo("##Value", current)) {
		// 今後形状を追加する場合はkTypesへ追加する
		for (ColliderShapeType candidate : kTypes) {
			const bool selected = candidate == type;
			if (ImGui::Selectable(ToString(candidate), selected)) {
				type = candidate;
				result.valueChanged = true;
				result.editFinished = true;
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
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
