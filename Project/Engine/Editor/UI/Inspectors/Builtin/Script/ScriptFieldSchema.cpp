#include "ScriptFieldInspector.h"

//============================================================================
//	include
//============================================================================

// c++

namespace Engine::ScriptFieldInspector {

	const char* KindToTypeString(Kind kind) {
		switch (kind) {
		case Kind::Bool: return "bool";
		case Kind::Byte: return "byte";
		case Kind::SByte: return "sbyte";
		case Kind::Short: return "short";
		case Kind::UShort: return "ushort";
		case Kind::Int: return "int";
		case Kind::UInt: return "uint";
		case Kind::Long: return "long";
		case Kind::ULong: return "ulong";
		case Kind::Float: return "float";
		case Kind::Double: return "double";
		case Kind::String: return "string";
		case Kind::Enum: return "enum";
		case Kind::Vector2: return "Vector2";
		case Kind::Vector3: return "Vector3";
		case Kind::Vector4: return "Vector4";
		case Kind::Quaternion: return "Quaternion";
		case Kind::Color3: return "Color3";
		case Kind::Color4: return "Color4";
		case Kind::Nullable: return "nullable";
		case Kind::Array: return "array";
		case Kind::List: return "list";
		case Kind::AssetRef: return "AssetRef";
		case Kind::EntityRef: return "EntityRef";
		case Kind::ScriptRef: return "ScriptRef";
		case Kind::ComponentRef: return "ComponentRef";
		case Kind::Object: return "object";
		case Kind::ManagedReference: return "managedReference";
		default: return "unsupported";
		}
	}

	Engine::AssetType AssetTypeFromName(const std::string& name) {
		if (name == "Texture") { return Engine::AssetType::Texture; }
		if (name == "Material") { return Engine::AssetType::Material; }
		if (name == "Mesh") { return Engine::AssetType::Mesh; }
		if (name == "Prefab") { return Engine::AssetType::Prefab; }
		if (name == "Scene") { return Engine::AssetType::Scene; }
		if (name == "Audio") { return Engine::AssetType::Audio; }
		if (name == "Font") { return Engine::AssetType::Font; }
		if (name == "AnimationClip") { return Engine::AssetType::AnimationClip; }
		if (name == "RenderPipeline") { return Engine::AssetType::RenderPipeline; }
		return Engine::AssetType::Unknown;
	}

	nlohmann::json DefaultForKind(const Engine::ManagedFieldSchema& field) {
		switch (field.kind) {
		case Kind::Bool: return false;
		case Kind::Byte: case Kind::SByte: case Kind::Short: case Kind::UShort:
		case Kind::Int: case Kind::UInt: case Kind::Long: case Kind::ULong:
			return 0;
		case Kind::Float: case Kind::Double: return 0.0;
		case Kind::String: return std::string{};
		case Kind::Enum:
			return field.enumValues.empty() ? nlohmann::json(0) : nlohmann::json(std::stoll(field.enumValues.front()));
		case Kind::Vector2: return nlohmann::json{ {"x", 0.0f}, {"y", 0.0f} };
		case Kind::Vector3: return nlohmann::json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f} };
		case Kind::Vector4: return nlohmann::json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 0.0f} };
		case Kind::Quaternion: return nlohmann::json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 1.0f} };
		case Kind::Color3: return nlohmann::json{ {"r", 0.0f}, {"g", 0.0f}, {"b", 0.0f} };
		case Kind::Color4: return nlohmann::json{ {"r", 0.0f}, {"g", 0.0f}, {"b", 0.0f}, {"a", 1.0f} };
		case Kind::Nullable: return nullptr;
		case Kind::Array: case Kind::List: return nlohmann::json::array();
		case Kind::AssetRef: return nlohmann::json{ {"assetId", ""} };
		case Kind::EntityRef: return nlohmann::json{ {"kind", "Null"}, {"sourceAsset", ""}, {"localFileId", ""} };
		case Kind::ScriptRef:
			return nlohmann::json{ {"entity", nlohmann::json{ {"kind", "Null"}, {"sourceAsset", ""}, {"localFileId", ""} }},
				{"scriptSlotId", ""}, {"scriptTypeId", ""} };
		case Kind::ComponentRef:
			return nlohmann::json{ {"entity", nlohmann::json{ {"kind", "Null"}, {"sourceAsset", ""}, {"localFileId", ""} }} };
		case Kind::Object: {
			nlohmann::json members = nlohmann::json::object();
			for (const auto& member : field.members) {
				if (member) { members[member->name] = DefaultForKind(*member); }
			}
			return members;
		}
		case Kind::ManagedReference:
			return nlohmann::json{ {"type", ""}, {"value", nlohmann::json::object()} };
		default: return nullptr;
		}
	}

	nlohmann::json ParseDefaultValue(const Engine::ManagedFieldSchema& field) {
		if (!field.defaultValueJson.empty() && field.defaultValueJson != "null") {
			try {
				return nlohmann::json::parse(field.defaultValueJson);
			}
			catch (const nlohmann::json::exception&) {
			}
		}
		return DefaultForKind(field);
	}

	Engine::Vector2 ReadVector2(const nlohmann::json& v) {
		Engine::Vector2 r{};
		if (v.is_object()) { r.x = v.value("x", 0.0f); r.y = v.value("y", 0.0f); }
		return r;
	}

	Engine::Vector3 ReadVector3(const nlohmann::json& v) {
		Engine::Vector3 r{};
		if (v.is_object()) { r.x = v.value("x", 0.0f); r.y = v.value("y", 0.0f); r.z = v.value("z", 0.0f); }
		return r;
	}

	Engine::Vector4 ReadVector4(const nlohmann::json& v) {
		Engine::Vector4 r{};
		if (v.is_object()) { r.x = v.value("x", 0.0f); r.y = v.value("y", 0.0f); r.z = v.value("z", 0.0f); r.w = v.value("w", 0.0f); }
		return r;
	}

	Engine::Quaternion ReadQuaternion(const nlohmann::json& v) {
		Engine::Quaternion r = Engine::Quaternion::Identity();
		if (v.is_object()) { r.x = v.value("x", 0.0f); r.y = v.value("y", 0.0f); r.z = v.value("z", 0.0f); r.w = v.value("w", 1.0f); }
		return r;
	}

	Engine::Color3 ReadColor3(const nlohmann::json& v) {
		Engine::Color3 r{};
		if (v.is_object()) { r.r = v.value("r", 0.0f); r.g = v.value("g", 0.0f); r.b = v.value("b", 0.0f); }
		return r;
	}

	Engine::Color4 ReadColor4(const nlohmann::json& v) {
		Engine::Color4 r{};
		if (v.is_object()) { r.r = v.value("r", 0.0f); r.g = v.value("g", 0.0f); r.b = v.value("b", 0.0f); r.a = v.value("a", 1.0f); }
		return r;
	}

	nlohmann::json WriteVector2(const Engine::Vector2& v) { return { {"x", v.x}, {"y", v.y} }; }

	nlohmann::json WriteVector3(const Engine::Vector3& v) { return { {"x", v.x}, {"y", v.y}, {"z", v.z} }; }

	nlohmann::json WriteVector4(const Engine::Vector4& v) { return { {"x", v.x}, {"y", v.y}, {"z", v.z}, {"w", v.w} }; }

	nlohmann::json WriteQuaternion(const Engine::Quaternion& v) { return { {"x", v.x}, {"y", v.y}, {"z", v.z}, {"w", v.w} }; }

	nlohmann::json WriteColor3(const Engine::Color3& v) { return { {"r", v.r}, {"g", v.g}, {"b", v.b} }; }

	nlohmann::json WriteColor4(const Engine::Color4& v) { return { {"r", v.r}, {"g", v.g}, {"b", v.b}, {"a", v.a} }; }

	void EnsureAuthoringSchema(Engine::ScriptEntry& entry, const Engine::ManagedScriptSchema& schema) {

		nlohmann::json& sf = entry.serializedFields;
		if (!sf.is_object() || !sf.contains("fields") || !sf["fields"].is_object()) {
			sf = nlohmann::json::object();
			sf["fields"] = nlohmann::json::object();
		}
		if (!sf.contains("unresolvedFields") || !sf["unresolvedFields"].is_object()) {
			sf["unresolvedFields"] = nlohmann::json::object();
		}
		sf["schemaVersion"] = schema.schemaVersion != 0 ? schema.schemaVersion : 2;
	}

	nlohmann::json& EnsureFieldValue(nlohmann::json& sf, const Engine::ManagedFieldSchema& field) {

		nlohmann::json& fields = sf["fields"];
		if (!fields.contains(field.fieldID) || !fields[field.fieldID].is_object()) {
			fields[field.fieldID] = nlohmann::json{
				{"name", field.name}, {"type", KindToTypeString(field.kind)}, {"value", ParseDefaultValue(field)} };
		}
		nlohmann::json& entry = fields[field.fieldID];
		// 名前と型は最新へ更新する
		entry["name"] = field.name;
		entry["type"] = KindToTypeString(field.kind);
		if (!entry.contains("value")) { entry["value"] = ParseDefaultValue(field); }
		return entry["value"];
	}
}
