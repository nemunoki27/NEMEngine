#include "ScriptFieldInspector.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>

// c++

namespace Engine::ScriptFieldInspector {

	bool DrawAuthoringField(const Engine::ManagedFieldSchema& field, nlohmann::json& sf,
		const DrawContext& ctx, bool& anyItemActive) {

		if (field.isHidden) {
			return false;
		}
		DrawHeaderIfAny(field);

		nlohmann::json& value = EnsureFieldValue(sf, field);
		const std::string label = FieldDisplayLabel(field);

		const Engine::ValueEditResult result = DrawValue(field, value, ctx, label.c_str());
		DrawTooltipIfAny(field);
		anyItemActive |= result.anyItemActive;
		return result.valueChanged;
	}

	void DrawRuntimeField(const Engine::ManagedFieldSchema& field, nlohmann::json& runtimeState,
		const DrawContext& ctx, Engine::BehaviorHandle handle) {

		if (field.isHidden) {
			return;
		}
		DrawHeaderIfAny(field);

		nlohmann::json& fieldValue = runtimeState[field.fieldID];
		if (fieldValue.is_null() && field.kind != Kind::Nullable) {
			fieldValue = ParseDefaultValue(field);
		}
		const std::string label = FieldDisplayLabel(field);

		Engine::ValueEditResult r = DrawValue(field, fieldValue, ctx, label.c_str());
		DrawTooltipIfAny(field);
		if (r.valueChanged) {
			// 実行中の実体だけへ即時反映する
			Engine::BehaviorSystem::SetRuntimeSerializedField(handle, field.fieldID, fieldValue);
		}
	}

	Engine::BehaviorHandle FindLiveHandle(Engine::ECSWorld& world, const Engine::Entity& entity,
		const Engine::UUID& scriptSlotID) {

		if (!world.IsAlive(entity) || !world.HasComponent<Engine::ScriptComponent>(entity)) {
			return Engine::BehaviorHandle::Null();
		}
		// 実行時ハンドルはECSへ複製せずBehaviorWorldから解決する
		return Engine::BehaviorSystem::FindRuntimeHandle(entity, scriptSlotID);
	}

	void DrawUnresolved(const nlohmann::json& sf) {

		if (!sf.contains("unresolvedFields") || !sf["unresolvedFields"].is_object() || sf["unresolvedFields"].empty()) {
			return;
		}
		if (ImGui::TreeNodeEx("未解決フィールド", ImGuiTreeNodeFlags_SpanAvailWidth)) {
			for (auto& [name, val] : sf["unresolvedFields"].items()) {
				MyGUI::Text(name.c_str(), val.dump());
			}
			ImGui::TreePop();
		}
	}
}
