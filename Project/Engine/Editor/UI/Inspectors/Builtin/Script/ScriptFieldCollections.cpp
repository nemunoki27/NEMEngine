#include "ScriptFieldInspector.h"

//============================================================================
//	include
//============================================================================

// c++
#include <memory>

namespace Engine::ScriptFieldInspector {

	Engine::ValueEditResult DrawCollection(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, const DrawContext& ctx) {

		Engine::ValueEditResult result{};
		if (!value.is_array()) { value = nlohmann::json::array(); }
		if (!field.element) {
			ImGui::TextDisabled("%s (要素schema無し)", label);
			return result;
		}

		// TreeNodeのIDは要素数を含めず安定させる、要素追加で開閉状態が消えないようにする
		ImGui::PushID(label);
		const bool open = ImGui::TreeNodeEx("##collection",
			ImGuiTreeNodeFlags_SpanAvailWidth, "%s [%zu]", label, value.size());
		if (open) {

			int removeIndex = -1;
			int moveFrom = -1;
			int moveTo = -1;
			for (size_t i = 0; i < value.size(); ++i) {

				ImGui::PushID(static_cast<int>(i));

				// 要素はTreeNodeで折りたたみ、ノード自体を掴んで別要素のノードへドロップすると並び替えできる
				const bool elementOpen = ImGui::TreeNodeEx("##element", ImGuiTreeNodeFlags_None, "要素 %d", static_cast<int>(i));
				if (!ctx.readOnly) {
					if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
						const int srcIndex = static_cast<int>(i);
						ImGui::SetDragDropPayload(kListElementDragDropType, &srcIndex, sizeof(int));
						ImGui::Text("要素 %d を移動", srcIndex);
						ImGui::EndDragDropSource();
					}
					if (ImGui::BeginDragDropTarget()) {
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kListElementDragDropType)) {
							if (payload->IsDelivery() && payload->DataSize == sizeof(int)) {
								moveFrom = *static_cast<const int*>(payload->Data);
								moveTo = static_cast<int>(i);
							}
						}
						ImGui::EndDragDropTarget();
					}
					// 削除はノードと同じ行に置く
					ImGui::SameLine();
					if (ImGui::SmallButton("削除")) { removeIndex = static_cast<int>(i); }
				}
				if (elementOpen) {

					Engine::ValueEditResult r{};
					if (field.element->kind == Kind::Object) {
						// 要素ノードの中で二重に折りたたまないようメンバを直接展開する
						r = DrawObjectMembers(value[i], field.element->members, ctx);
					} else {
						r = DrawValue(*field.element, value[i], ctx, "値");
					}
					if (r.valueChanged) { result.valueChanged = true; }
					result.anyItemActive |= r.anyItemActive;
					result.editFinished |= r.editFinished;
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			if (!ctx.readOnly) {
				ImGui::Separator();
				if (ImGui::SmallButton("要素追加")) {
					value.push_back(DefaultForKind(*field.element));
					result.valueChanged = true;
					result.editFinished = true;
				}
				if (removeIndex >= 0) {
					value.erase(value.begin() + removeIndex);
					result.valueChanged = true;
					result.editFinished = true;
				}
				// ドラッグした要素がドロップ先のindexへ来るよう挿入し直して並び替える
				if (moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo) {
					nlohmann::json moved = value[moveFrom];
					value.erase(value.begin() + moveFrom);
					value.insert(value.begin() + moveTo, moved);
					result.valueChanged = true;
					result.editFinished = true;
				}
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
		return result;
	}

	Engine::ValueEditResult DrawNullable(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, const DrawContext& ctx) {

		Engine::ValueEditResult result{};
		bool hasValue = !value.is_null();

		if (Engine::MyGUI::BeginPropertyRow(label)) {
			if (ImGui::Checkbox("##hasValue", &hasValue)) {
				if (hasValue && field.element) { value = DefaultForKind(*field.element); }
				else { value = nullptr; }
				result.valueChanged = true;
				result.editFinished = true;
			}
			Engine::MyGUI::EndPropertyRow();
		}
		if (!value.is_null() && field.element) {
			Engine::ValueEditResult r = DrawValue(*field.element, value, ctx, "  value");
			if (r.valueChanged) { result.valueChanged = true; }
			result.anyItemActive |= r.anyItemActive;
		}
		return result;
	}

	Engine::ValueEditResult DrawObjectMembers(nlohmann::json& value,
		const std::vector<std::shared_ptr<Engine::ManagedFieldSchema>>& members, const DrawContext& ctx) {

		Engine::ValueEditResult result{};
		if (!value.is_object()) { value = nlohmann::json::object(); }
		for (const auto& member : members) {

			if (!member || member->isHidden) { continue; }
			// 欠落メンバは既定値で補完する
			if (!value.contains(member->name)) { value[member->name] = DefaultForKind(*member); }

			const std::string memberLabel = member->label.empty() ? member->name : member->label;
			Engine::ValueEditResult r = DrawValue(*member, value[member->name], ctx, memberLabel.c_str());
			if (r.valueChanged) { result.valueChanged = true; }
			result.anyItemActive |= r.anyItemActive;
			result.editFinished |= r.editFinished;
		}
		return result;
	}

	Engine::ValueEditResult DrawObject(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, const DrawContext& ctx) {

		Engine::ValueEditResult result{};
		if (field.members.empty()) {
			ImGui::TextDisabled("%s (編集できるメンバ無し)", label);
			return result;
		}

		ImGui::PushID(label);
		const bool open = ImGui::TreeNodeEx("##object", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", label);
		if (open) {
			result = DrawObjectMembers(value, field.members, ctx);
			ImGui::TreePop();
		}
		ImGui::PopID();
		return result;
	}

	Engine::ValueEditResult DrawManagedReference(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, const DrawContext& ctx) {

		Engine::ValueEditResult result{};
		if (!value.is_object()) { value = nlohmann::json{ {"type", ""}, {"value", nlohmann::json::object()} }; }
		if (!value.contains("type") || !value["type"].is_string()) { value["type"] = ""; }
		if (!value.contains("value") || !value["value"].is_object()) { value["value"] = nlohmann::json::object(); }

		ImGui::PushID(label);

		// 派生型の選択、Noneで未設定に戻す
		const std::string currentType = value["type"].get<std::string>();
		if (Engine::MyGUI::BeginPropertyRow(label)) {
			const std::string preview = currentType.empty() ? "None" : ToShortTypeName(currentType);
			if (ImGui::BeginCombo("##managedRefType", preview.c_str())) {
				if (ImGui::Selectable("None", currentType.empty()) && !currentType.empty()) {
					value["type"] = "";
					value["value"] = nlohmann::json::object();
					result.valueChanged = true;
					result.editFinished = true;
				}
				for (const auto& candidate : field.candidates) {
					if (ImGui::Selectable(ToShortTypeName(candidate.type).c_str(), candidate.type == currentType) &&
						candidate.type != currentType) {

						// 型変更時は選択型の既定値で値を作り直す
						nlohmann::json members = nlohmann::json::object();
						for (const auto& member : candidate.members) {
							if (member) { members[member->name] = DefaultForKind(*member); }
						}
						value["type"] = candidate.type;
						value["value"] = std::move(members);
						result.valueChanged = true;
						result.editFinished = true;
					}
				}
				ImGui::EndCombo();
			}
			result.anyItemActive |= ImGui::IsItemActive();
			Engine::MyGUI::EndPropertyRow();
		}

		// 選択型のメンバ編集、候補から外れた保存型は値を保持したまま欠落表示にする
		const std::string selectedType = value["type"].get<std::string>();
		if (!selectedType.empty()) {

			const Engine::ManagedFieldSchema::ReferenceCandidate* selected = nullptr;
			for (const auto& candidate : field.candidates) {
				if (candidate.type == selectedType) {
					selected = &candidate;
					break;
				}
			}
			if (selected) {
				Engine::ValueEditResult r = DrawObjectMembers(value["value"], selected->members, ctx);
				if (r.valueChanged) { result.valueChanged = true; }
				result.anyItemActive |= r.anyItemActive;
				result.editFinished |= r.editFinished;
			} else {
				ImGui::TextDisabled("  Missing type | %s", selectedType.c_str());
			}
		}
		ImGui::PopID();
		return result;
	}
}
