#include "ScriptInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Inspector/Methods/Common/InspectorDrawerCommon.h>
#include <Engine/Core/ECS/Behavior/Registry/BehaviorTypeRegistry.h>
#include <Engine/Core/Scripting/ManagedScriptRuntime.h>
#include <Engine/Editor/Scripting/ScriptAssetDragDrop.h>
#include <Engine/Utility/ImGui/MyGUI.h>

//============================================================================
//	ScriptInspectorDrawer classMethods
//============================================================================

namespace {

	// ScriptEntryを生成する
	Engine::ScriptEntry MakeScriptEntry(const std::string& typeName, Engine::AssetID scriptAsset = {}) {

		Engine::ScriptEntry entry{};
		entry.type = typeName;
		entry.scriptAsset = scriptAsset;
		entry.enabled = true;
		entry.serializedFields = nlohmann::json::object();
		entry.handle = Engine::BehaviorHandle::Null();
		return entry;
	}

	// JSON文字列をフィールドの初期値として読み込む
	nlohmann::json ParseDefaultValue(const std::string& defaultValueJson) {

		if (defaultValueJson.empty()) {
			return {};
		}

		try {
			return nlohmann::json::parse(defaultValueJson);
		}
		catch (const nlohmann::json::exception&) {
			return {};
		}
	}

	// フィールドが未保存ならC#側の初期値を保存する
	bool EnsureFieldValue(nlohmann::json& fields, const Engine::ManagedScriptField& field) {

		if (!fields.is_object()) {
			fields = nlohmann::json::object();
		}
		if (fields.contains(field.name)) {
			return false;
		}

		fields[field.name] = ParseDefaultValue(field.defaultValueJson);
		return true;
	}

	// Vector3をJSONから読み込む
	Engine::Vector3 ReadVector3(const nlohmann::json& value) {

		Engine::Vector3 result{};
		if (!value.is_object()) {
			return result;
		}

		result.x = value.value("x", 0.0f);
		result.y = value.value("y", 0.0f);
		result.z = value.value("z", 0.0f);
		return result;
	}

	// Vector2をJSONから読み込む
	Engine::Vector2 ReadVector2(const nlohmann::json& value) {

		Engine::Vector2 result{};
		if (!value.is_object()) {
			return result;
		}

		result.x = value.value("x", 0.0f);
		result.y = value.value("y", 0.0f);
		return result;
	}

	// Vector3をJSONへ保存する
	nlohmann::json WriteVector3(const Engine::Vector3& value) {

		nlohmann::json out = nlohmann::json::object();
		out["x"] = value.x;
		out["y"] = value.y;
		out["z"] = value.z;
		return out;
	}

	// Vector2をJSONへ保存する
	nlohmann::json WriteVector2(const Engine::Vector2& value) {

		nlohmann::json out = nlohmann::json::object();
		out["x"] = value.x;
		out["y"] = value.y;
		return out;
	}

	// C#側のフィールド型に応じた編集UIを描画する
	Engine::ValueEditResult DrawManagedField(Engine::ScriptEntry& entry, const Engine::ManagedScriptField& field) {

		EnsureFieldValue(entry.serializedFields, field);
		nlohmann::json& value = entry.serializedFields[field.name];

		switch (field.kind) {
		case Engine::ManagedSerializedFieldKind::Bool: {
			bool v = value.is_boolean() ? value.get<bool>() : false;
			Engine::ValueEditResult result = Engine::InspectorDrawerCommon::DrawCheckboxField(field.displayName.c_str(), v);
			if (result.valueChanged) {
				value = v;
			}
			return result;
		}
		case Engine::ManagedSerializedFieldKind::Int: {
			int32_t v = value.is_number_integer() ? value.get<int32_t>() : 0;
			Engine::ValueEditResult result = Engine::MyGUI::DragInt(field.displayName.c_str(), v);
			if (result.valueChanged) {
				value = v;
			}
			return result;
		}
		case Engine::ManagedSerializedFieldKind::Float: {
			float v = value.is_number() ? value.get<float>() : 0.0f;
			Engine::ValueEditResult result = Engine::MyGUI::DragFloat(field.displayName.c_str(), v);
			if (result.valueChanged) {
				value = v;
			}
			return result;
		}
		case Engine::ManagedSerializedFieldKind::Double: {
			float v = value.is_number() ? value.get<float>() : 0.0f;
			Engine::ValueEditResult result = Engine::MyGUI::DragFloat(field.displayName.c_str(), v);
			if (result.valueChanged) {
				value = static_cast<double>(v);
			}
			return result;
		}
		case Engine::ManagedSerializedFieldKind::String: {
			std::string v = value.is_string() ? value.get<std::string>() : std::string{};
			Engine::ValueEditResult result = Engine::MyGUI::InputText(field.displayName.c_str(), v);
			if (result.valueChanged) {
				value = v;
			}
			return result;
		}
		case Engine::ManagedSerializedFieldKind::Vector3: {
			Engine::Vector3 v = ReadVector3(value);
			Engine::ValueEditResult result = Engine::MyGUI::DragVector3(field.displayName.c_str(), v);
			if (result.valueChanged) {
				value = WriteVector3(v);
			}
			return result;
		}
		case Engine::ManagedSerializedFieldKind::Vector2: {
			Engine::Vector2 v = ReadVector2(value);
			Engine::ValueEditResult result = Engine::MyGUI::DragVector2(field.displayName.c_str(), v);
			if (result.valueChanged) {
				value = WriteVector2(v);
			}
			return result;
		}
		default:
			break;
		}
		return {};
	}

	// Scriptアセットの参照フィールドを描画する
	Engine::ValueEditResult DrawScriptAssetField(const Engine::EditorPanelContext& context, Engine::ScriptEntry& entry) {

		Engine::AssetID beforeAsset = entry.scriptAsset;
		Engine::ValueEditResult result = Engine::MyGUI::AssetReferenceField("Script Asset", entry.scriptAsset,
			context.editorContext ? context.editorContext->assetDatabase : nullptr, { Engine::AssetType::Script });
		if (!result.valueChanged) {
			return result;
		}

		std::string typeName{};
		if (!Engine::ScriptAssetDragDrop::ResolveScriptTypeName(context, entry.scriptAsset, typeName)) {

			// C# DLLに存在しないScriptアセットはInspectorへ反映しない
			entry.scriptAsset = beforeAsset;
			result.valueChanged = false;
			result.editFinished = false;
			return result;
		}

		if (entry.type != typeName) {

			entry.type = typeName;
			entry.serializedFields = nlohmann::json::object();
		}
		return result;
	}

	// ScriptアセットのドロップでScriptEntryを追加する
	Engine::ValueEditResult DrawScriptDropField(const Engine::EditorPanelContext& context, Engine::ScriptComponent& component) {

		Engine::ValueEditResult result{};
		if (!Engine::MyGUI::BeginPropertyRow("Script Asset")) {
			return result;
		}

		ImGui::Button("Drop C# Script", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()));
		result.anyItemActive = ImGui::IsItemActive();

		if (ImGui::BeginDragDropTarget()) {

			Engine::AssetID scriptAsset{};
			std::string typeName{};
			if (Engine::ScriptAssetDragDrop::AcceptScriptAssetDrop(context, scriptAsset, typeName)) {

				component.scripts.emplace_back(MakeScriptEntry(typeName, scriptAsset));
				result.valueChanged = true;
				result.editFinished = true;
			}
			ImGui::EndDragDropTarget();
		}

		Engine::MyGUI::EndPropertyRow();
		return result;
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

			{
				ValueEditResult result = DrawScriptAssetField(context, entry);
				PushEditResult(result, anyItemActive);
				ImGui::Separator();
			}
			{
				ValueEditResult result = InspectorDrawerCommon::DrawBehaviorTypeField("Type", entry.type);
				if (result.valueChanged) {

					// 型を手動で変えた場合は、Scriptアセットとの対応を切って古いフィールド値を持ち越さない
					entry.scriptAsset = {};
					entry.serializedFields = nlohmann::json::object();
				}
				PushEditResult(result, anyItemActive);
				ImGui::Separator();
			}
			DrawField(anyItemActive, [&]() {
				return InspectorDrawerCommon::DrawCheckboxField("Enabled", entry.enabled);
				});

			// C#側から取得した[SerializeField]対象をインスペクターへ表示する
			const auto& fields = ManagedScriptRuntime::GetInstance().GetSerializedFields(entry.type);
			if (!fields.empty()) {
				ImGui::TextDisabled("Serialized Fields");
				for (const auto& field : fields) {

					DrawField(anyItemActive, [&]() {
						return DrawManagedField(entry, field);
						});
				}
			}

			// スクリプトの削除
			if (ImGui::Button("Remove Script")) {
				removeIndex = static_cast<int32_t>(i);
			}
			ImGui::TreePop();
		}
		ImGui::Separator();
		ImGui::PopID();
	}

	// ScriptアセットをドロップしてScriptEntryを追加する
	DrawField(anyItemActive, [&]() {
		return DrawScriptDropField(context, draft);
		});

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

				// メニューアイテムが選択されたらスクリプトエントリを追加してポップアップを閉じる
				if (ImGui::MenuItem(info.name.c_str())) {
					draft.scripts.emplace_back(MakeScriptEntry(info.name));
					RequestCommit();
					ImGui::CloseCurrentPopup();
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
