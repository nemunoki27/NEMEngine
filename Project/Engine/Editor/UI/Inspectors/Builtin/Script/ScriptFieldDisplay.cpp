#include "ScriptFieldInspector.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneEntityReference.h>

namespace Engine::ScriptFieldInspector {
	namespace {

		// 欠落値は保存データを変更せず空値として読む
		const nlohmann::json& ReadMember(const nlohmann::json& value, const std::string& name) {

			static const nlohmann::json empty;
			if (!value.is_object()) return empty;
			const auto found = value.find(name);
			return found == value.end() ? empty : *found;
		}

		// 保存済みの参照を名前で表示する
		std::string EntityDisplayName(const nlohmann::json& value, const DrawContext& ctx) {

			if (!value.is_object()) return "未設定";
			SceneEntityReference reference;
			from_json(value, reference);
			if (!reference.localFileID) return "未設定";
			if (ctx.world) {
				const auto target = reference.Resolve(*ctx.world, ctx.owner);
				if (const auto* name = ctx.world->TryGetComponent<NameComponent>(target)) return name->name;
			}
			return "参照先が見つかりません: " + ToString(reference.localFileID);
		}

		// 入れ子の値も編集UIを生成せず表示する
		void DrawReadOnlyMembers(const nlohmann::json& value,
			const std::vector<std::shared_ptr<const ManagedFieldSchema>>& members, const DrawContext& ctx) {

			for (const auto& member : members) {
				if (!member || member->isHidden) continue;
				ImGui::PushID(member->name.c_str());
				DrawReadOnlyValue(*member, ReadMember(value, member->name), ctx, FieldDisplayLabel(*member).c_str());
				ImGui::PopID();
			}
		}
	}

	void DrawReadOnlyValue(const ManagedFieldSchema& field, const nlohmann::json& value,
		const DrawContext& ctx, const char* label) {

		switch (field.kind) {
		case Kind::Bool: MyGUI::TextBool(label, value.is_boolean() && value.get<bool>()); return;
		case Kind::Byte:
		case Kind::UShort:
		case Kind::UInt: MyGUI::TextUInt(label, value.is_number_integer() ? value.get<uint32_t>() : 0); return;
		case Kind::SByte:
		case Kind::Short:
		case Kind::Int: MyGUI::TextInt(label, value.is_number_integer() ? value.get<int32_t>() : 0); return;
		case Kind::Long: MyGUI::TextInt64(label, value.is_number_integer() ? value.get<int64_t>() : 0); return;
		case Kind::ULong: MyGUI::TextUInt64(label, value.is_number_integer() ? value.get<uint64_t>() : 0); return;
		case Kind::Float: MyGUI::TextFloat(label, value.is_number() ? value.get<float>() : 0); return;
		case Kind::Double: MyGUI::TextDouble(label, value.is_number() ? value.get<double>() : 0); return;
		case Kind::String: MyGUI::Text(label, value.is_string() ? value.get<std::string>() : ""); return;
		case Kind::Vector2: MyGUI::TextVector2(label, ReadVector2(value)); return;
		case Kind::Vector3: MyGUI::TextVector3(label, ReadVector3(value)); return;
		case Kind::Vector4: MyGUI::TextVector4(label, ReadVector4(value)); return;
		case Kind::Quaternion: MyGUI::TextQuaternion(label, ReadQuaternion(value)); return;
		case Kind::Color3: MyGUI::TextColor3(label, ReadColor3(value)); return;
		case Kind::Color4: MyGUI::TextColor4(label, ReadColor4(value)); return;
		case Kind::Enum: {
			const std::string text = value.is_number_integer() ? value.dump() : "0";
			for (size_t i = 0; i < field.enumValues.size() && i < field.enumNames.size(); ++i) {
				if (field.enumValues[i] == text) { MyGUI::Text(label, field.enumNames[i]); return; }
			}
			MyGUI::Text(label, text);
			return;
		}
		case Kind::Nullable:
			if (!value.is_null() && field.element) DrawReadOnlyValue(*field.element, value, ctx, label);
			else MyGUI::Text(label, "未設定");
			return;
		case Kind::AssetRef: {
			const auto& stored = ReadMember(value, "assetId");
			const std::string id = stored.is_string() ? stored.get<std::string>() : "";
			const auto* database = ctx.panel && ctx.panel->editorContext ? ctx.panel->editorContext->assetDatabase : nullptr;
			const auto* asset = database ? database->Find(FromString32Hex(id)) : nullptr;
			MyGUI::Text(label, asset ? asset->assetPath : id.empty() ? "未設定" : id);
			return;
		}
		case Kind::EntityRef: MyGUI::Text(label, EntityDisplayName(value, ctx)); return;
		case Kind::ScriptRef:
		case Kind::ComponentRef: MyGUI::Text(label, EntityDisplayName(ReadMember(value, "entity"), ctx)); return;
		case Kind::Array:
		case Kind::List:
		case Kind::Object:
		case Kind::ManagedReference: break;
		default: MyGUI::Text(label, "未対応の型"); return;
		}

		ImGui::PushID(label);
		if (ImGui::TreeNodeEx("##ReadOnly", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", label)) {
			if (field.kind == Kind::Array || field.kind == Kind::List) {
				MyGUI::TextUInt64("要素数", value.is_array() ? value.size() : 0);
				if (value.is_array() && field.element) {
					for (size_t i = 0; i < value.size(); ++i) {
						ImGui::PushID(static_cast<int>(i));
						DrawReadOnlyValue(*field.element, value[i], ctx, ("要素 " + std::to_string(i)).c_str());
						ImGui::PopID();
					}
				}
			} else if (field.kind == Kind::Object) {
				DrawReadOnlyMembers(value, field.members, ctx);
			} else if (const auto& reference = ReadMember(value, "$ref"); !reference.is_null()) {
				MyGUI::Text("共有参照", reference.dump());
			} else {
				const auto& stored = ReadMember(value, "type");
				const std::string type = stored.is_string() ? stored.get<std::string>() : "";
				MyGUI::Text("型", type.empty() ? "未設定" : type);
				for (const auto& candidate : field.candidates) if (candidate.type == type) {
					DrawReadOnlyMembers(ReadMember(value, "value"), candidate.members, ctx);
					break;
				}
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
}
