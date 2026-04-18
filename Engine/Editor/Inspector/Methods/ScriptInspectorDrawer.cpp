#include "ScriptInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Inspector/Methods/Common/InspectorDrawerCommon.h>
#include <Engine/Core/ECS/Behavior/Registry/BehaviorTypeRegistry.h>
#include <Engine/Utility/ImGui/MyGUI.h>

//============================================================================
//	ScriptInspectorDrawer classMethods
//============================================================================

namespace {

	// スクリプトコンポーネントに指定した型のスクリプトがあるか
	bool HasScriptType(const Engine::ScriptComponent& component, std::string_view typeName) {

		for (const auto& entry : component.scripts) {
			if (entry.type == typeName) {
				return true;
			}
		}
		return false;
	}
}

void Engine::ScriptInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	// ドラフトコンポーネントを参照
	auto& draft = GetDraft();

	// スクリプトエントリごとの描画
	int32_t removeIndex = -1;
	for (size_t i = 0; i < draft.scripts.size(); ++i) {

		ImGui::PushID(static_cast<int32_t>(i));

		// スクリプト
		ScriptEntry& entry = draft.scripts[i];
		const std::string headerText = entry.type.empty() ? ("Script " + std::to_string(i)) : entry.type;

		//============================================================================
		//	スクリプト表示
		//============================================================================
		if (ImGui::TreeNodeEx("##ScriptEntry", ImGuiTreeNodeFlags_DefaultOpen |
			ImGuiTreeNodeFlags_SpanAvailWidth, "%s", headerText.c_str())) {

			DrawField(anyItemActive, [&]() {
				return InspectorDrawerCommon::DrawBehaviorTypeField("Type", entry.type);
				});
			DrawField(anyItemActive, [&]() {
				return InspectorDrawerCommon::DrawCheckboxField("Enabled", entry.enabled);
				});

			// スクリプトの削除
			if (ImGui::Button("Remove Script")) {
				removeIndex = static_cast<int32_t>(i);
			}
			ImGui::TreePop();
		}
		ImGui::Separator();
		ImGui::PopID();
	}

	// スクリプト削除
	if (0 <= removeIndex) {

		draft.scripts.erase(draft.scripts.begin() + removeIndex);
		RequestCommit();
	}
	// スクリプト追加
	if (ImGui::Button("Add Script")) {

		ImGui::OpenPopup("##AddScriptPopup");
	}

	//============================================================================
	//	スクリプト追加ポップアップ
	//============================================================================
	if (ImGui::BeginPopup("##AddScriptPopup")) {

		const auto& registry = BehaviorTypeRegistry::GetInstance();

		if (registry.GetBehaviorTypeCount() == 0) {

			ImGui::TextDisabled("No registered behaviors.");
		} else {
			// 登録されているスクリプトの型をメニューアイテムとして表示
			for (uint32_t i = 0; i < registry.GetBehaviorTypeCount(); ++i) {

				const auto& info = registry.GetInfo(i);
				const bool alreadyExists = HasScriptType(draft, info.name);
				// すでに同じ型のスクリプトがある場合は追加できないようにする
				if (alreadyExists) {
					ImGui::BeginDisabled();
				}

				// メニューアイテムが選択されたらスクリプトエントリを追加してポップアップを閉じる
				if (ImGui::MenuItem(info.name.c_str())) {
					draft.scripts.emplace_back(ScriptEntry{
						.type = info.name,
						.enabled = true,
						.handle = BehaviorHandle::Null()
						});
					RequestCommit();
					ImGui::CloseCurrentPopup();
				}
				if (alreadyExists) {
					ImGui::EndDisabled();
				}
			}
		}
		ImGui::EndPopup();
	}
	// スクリプトが1つもない場合
	if (draft.scripts.empty()) {
		ImGui::TextDisabled("No scripts attached.");
	}
}