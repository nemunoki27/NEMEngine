#include "TimelineTool.h"
#include "TimelineClipFields.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Script/ScriptFieldInspector.h>

// c++
#include <unordered_map>

using namespace Engine;

namespace {

	// 独自クリップの型情報を既存のScript編集形式へ合わせる
	ManagedFieldSchema MakeCustomField(const nlohmann::json& source) {

		using Kind = ManagedSerializedFieldKind;
		static const std::unordered_map<std::string, Kind> kinds{
			{"Bool", Kind::Bool}, {"Int", Kind::Int}, {"Float", Kind::Float}, {"Double", Kind::Double},
			{"Vector2", Kind::Vector2}, {"Vector3", Kind::Vector3}, {"Vector4", Kind::Vector4},
			{"Color3", Kind::Color3}, {"Color4", Kind::Color4}, {"Quaternion", Kind::Quaternion},
			{"String", Kind::String}, {"Enum", Kind::Enum}, {"Asset", Kind::AssetRef}, {"Object", Kind::Object},
			{"Byte", Kind::Byte}, {"SByte", Kind::SByte}, {"Short", Kind::Short}, {"UShort", Kind::UShort},
			{"UInt", Kind::UInt}, {"Long", Kind::Long}, {"ULong", Kind::ULong}
		};
		ManagedFieldSchema field;
		field.fieldID = source.value("id", "");
		field.name = source.value("name", "");
		field.label = source.value("label", field.name);
		const auto kind = kinds.find(source.value("kind", "Object"));
		field.kind = kind == kinds.end() ? Kind::Unsupported : kind->second;
		field.assetType = source.value("assetType", "");
		field.componentType = source.value("componentType", "");
		field.tooltip = source.value("tooltip", "");
		field.isHidden = source.value("hidden", false);
		field.isReadOnly = source.value("readOnly", false);
		if (source.contains("default")) field.defaultValueJSON = source.at("default").dump();
		if (source.contains("enumOptions")) for (const auto& entry : source.at("enumOptions")) {
			field.enumNames.push_back(entry.value("name", ""));
			field.enumValues.push_back(entry.at("value").is_string() ? entry.at("value").get<std::string>() : entry.at("value").dump());
		}
		field.enumUnderlying = "Int";
		if (source.contains("min")) { field.hasMin = true; field.minValue = source.at("min").get<float>(); }
		if (source.contains("min") && source.contains("max")) {
			field.hasRange = true;
			field.rangeMin = source.at("min").get<float>();
			field.rangeMax = source.at("max").get<float>();
		} else if (source.contains("rangeMin") && source.contains("rangeMax")) {
			field.hasRange = true;
			field.rangeMin = source.at("rangeMin").get<float>();
			field.rangeMax = source.at("rangeMax").get<float>();
		}
		return field;
	}

	// ImGuiの編集範囲へ入る前に型付きFieldの値を検証する
	bool ValidateCustomFieldValue(const ManagedFieldSchema& field, const nlohmann::json& value) {

		using namespace ScriptFieldInspector;
		try {
			switch (field.kind) {
			case Kind::Vector2: ReadVector2(value); break;
			case Kind::Vector3: ReadVector3(value); break;
			case Kind::Vector4: ReadVector4(value); break;
			case Kind::Quaternion: ReadQuaternion(value); break;
			case Kind::Color3: ReadColor3(value); break;
			case Kind::Color4: ReadColor4(value); break;
			case Kind::AssetRef:
				if (value.is_object() && value.contains("assetId") && !value.at("assetId").is_string()) return false;
				break;
			default: break;
			}
			if (value.is_object() && value.contains("binding") && !value.at("binding").is_string()) return false;
			return true;
		} catch (const std::exception&) { return false; }
	}
}

void TimelineTool::DrawCustomClipData(const EditorToolContext& context, TimelineClip& clip) {

	if (nextCatalogUpdate_ <= ImGui::GetTime()) {
		nextCatalogUpdate_ = ImGui::GetTime() + 2.0;
		const auto catalogue = nlohmann::json::parse(ManagedScriptRuntime::GetInstance().CopyTimelineClipTypes(), nullptr, false);
		customCatalog_ = catalogue.is_array() ? catalogue : nlohmann::json::array();
	}
	std::string type = clip.data.value("typeName", "");
	const nlohmann::json* selectedType = nullptr;
	if (MyGUI::BeginPropertyRow("C#クリップ")) {
		if (ImGui::BeginCombo("##CustomClipType", type.empty() ? "選択してください" : type.c_str())) {
			for (const auto& candidate : customCatalog_) {
				const std::string id = candidate.value("id", candidate.value("typeName", ""));
				const std::string name = candidate.value("displayName", candidate.value("typeName", id));
				const bool selected = type == id || type == candidate.value("typeName", "");
				if (ImGui::Selectable(name.c_str(), selected)) {
					type = id;
					clip.data["typeName"] = id;
					clip.data["fields"] = nlohmann::json::object();
				}
			}
			ImGui::EndCombo();
		}
		MyGUI::EndPropertyRow();
	}
	for (const auto& candidate : customCatalog_) {
		if (type == candidate.value("id", "") || type == candidate.value("typeName", "")) selectedType = &candidate;
	}
	if (!selectedType) {
		TimelineClipFields::Text("型ID・型名", clip.data, "typeName");
		MyGUI::Text("C#", "TimelineClipを継承したクラスを作成し、ゲームスクリプトをビルドしてください");
		return;
	}
	MyGUI::TextBool("編集中の評価に対応", selectedType->value("supportsPreview", false));
	if (!selectedType->value("supportsPreview", false)) MyGUI::Text("プレビュー", "このC#クリップはPlay中のみ実行されます");
	if (!clip.data.contains("fields") || !clip.data.at("fields").is_object()) clip.data["fields"] = nlohmann::json::object();
	auto& fields = clip.data["fields"];
	if (!selectedType->contains("fields")) return;
	ScriptFieldInspector::DrawContext drawContext;
	drawContext.panel = context.panelContext;
	drawContext.world = context.GetWorld();
	drawContext.owner = session_.GetOwner(context);
	for (const auto& metadata : selectedType->at("fields")) {
		const auto field = MakeCustomField(metadata);
		if (field.isHidden) continue;
		const std::string key = field.fieldID.empty() ? field.name : field.fieldID;
		if (!fields.contains(key)) fields[key] = metadata.value("default", nlohmann::json());
		const bool valid = ValidateCustomFieldValue(field, fields[key]);
		ImGui::PushID(key.c_str());
		if (!valid) {
			MyGUI::Text(field.label.c_str(), "保存された値の型が正しくありません");
			if (ImGui::Button("フィールドを初期化")) fields[key] = ScriptFieldInspector::ParseDefaultValue(field);
			ImGui::PopID();
			continue;
		}
		const std::string kind = metadata.value("kind", "");
		if (kind == "GameObject" || kind == "Component") {
			ImGui::BeginDisabled(field.isReadOnly);
			if (!fields[key].is_object()) fields[key] = nlohmann::json::object();
			TimelineClipFields::Binding(field.label.c_str(), fields[key], "binding", session_.GetAsset());
			ImGui::EndDisabled();
		} else {
			ScriptFieldInspector::DrawValue(field, fields[key], drawContext, field.label.c_str());
		}
		ImGui::PopID();
	}
}
