#include "ManagedSchemaCache.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>

#include <json.hpp>

namespace {

	// スキーマJSONのkind文字列を列挙へ
	Engine::ManagedSerializedFieldKind ParseFieldKind(const std::string& kind) {

		using K = Engine::ManagedSerializedFieldKind;
		static const std::unordered_map<std::string, K> kMap = {
			{ "Bool", K::Bool }, { "Byte", K::Byte }, { "SByte", K::SByte }, { "Short", K::Short },
			{ "UShort", K::UShort }, { "Int", K::Int }, { "UInt", K::UInt }, { "Long", K::Long },
			{ "ULong", K::ULong }, { "Float", K::Float }, { "Double", K::Double }, { "String", K::String },
			{ "Enum", K::Enum }, { "Vector2", K::Vector2 }, { "Vector3", K::Vector3 }, { "Vector4", K::Vector4 },
			{ "Quaternion", K::Quaternion }, { "Color3", K::Color3 }, { "Color4", K::Color4 },
			{ "Nullable", K::Nullable }, { "Array", K::Array }, { "List", K::List },
			{ "AssetRef", K::AssetRef }, { "EntityRef", K::EntityRef }, { "ScriptRef", K::ScriptRef },
			{ "ComponentRef", K::ComponentRef },
			{ "Object", K::Object }, { "ManagedReference", K::ManagedReference },
		};
		auto it = kMap.find(kind);
		return it != kMap.end() ? it->second : K::Unsupported;
	}

	// 1フィールドのスキーマノードを解析する、配列やnullableは要素を再帰する
	Engine::ManagedFieldSchema ParseFieldSchema(const nlohmann::json& node) {

		Engine::ManagedFieldSchema field{};
		field.fieldID = node.value("fieldId", std::string{});
		field.name = node.value("name", std::string{});
		field.declaringType = node.value("declaringType", std::string{});
		field.kind = ParseFieldKind(node.value("kind", std::string("Unsupported")));
		field.isPublic = node.value("isPublic", false);
		field.isReadOnly = node.value("isReadOnly", false);
		field.isHidden = node.value("isHidden", false);
		field.multiline = node.value("multiline", false);
		field.tooltip = node.value("tooltip", std::string{});
		field.header = node.value("header", std::string{});
		field.label = node.value("label", std::string{});
		field.enumUnderlying = node.value("enumUnderlying", std::string{});
		field.assetType = node.value("assetType", std::string{});
		field.scriptType = node.value("scriptType", std::string{});
		field.componentType = node.value("componentType", std::string{});
		field.defaultValueJson = node.value("defaultValueJson", std::string("null"));

		if (node.contains("range") && node["range"].is_object()) {
			field.hasRange = true;
			field.rangeMin = node["range"].value("min", 0.0f);
			field.rangeMax = node["range"].value("max", 0.0f);
		}
		if (node.contains("min") && node["min"].is_number()) {
			field.hasMin = true;
			field.minValue = node["min"].get<float>();
		}
		if (node.contains("dragSpeed") && node["dragSpeed"].is_number()) {
			field.hasDragSpeed = true;
			field.dragSpeed = node["dragSpeed"].get<float>();
		}
		if (node.contains("enumNames") && node["enumNames"].is_array()) {
			for (const auto& n : node["enumNames"]) {
				field.enumNames.push_back(n.get<std::string>());
			}
		}
		if (node.contains("enumValues") && node["enumValues"].is_array()) {
			for (const auto& v : node["enumValues"]) {
				field.enumValues.push_back(v.get<std::string>());
			}
		}
		if (node.contains("element") && node["element"].is_object()) {
			field.element = std::make_shared<Engine::ManagedFieldSchema>(ParseFieldSchema(node["element"]));
		}
		field.objectType = node.value("objectType", std::string{});
		if (node.contains("members") && node["members"].is_array()) {
			for (const auto& memberNode : node["members"]) {
				field.members.emplace_back(std::make_shared<Engine::ManagedFieldSchema>(ParseFieldSchema(memberNode)));
			}
		}
		if (node.contains("candidates") && node["candidates"].is_array()) {
			for (const auto& candidateNode : node["candidates"]) {

				Engine::ManagedFieldSchema::ReferenceCandidate candidate{};
				candidate.type = candidateNode.value("type", std::string{});
				if (candidateNode.contains("members") && candidateNode["members"].is_array()) {
					for (const auto& memberNode : candidateNode["members"]) {
						candidate.members.emplace_back(std::make_shared<Engine::ManagedFieldSchema>(ParseFieldSchema(memberNode)));
					}
				}
				field.candidates.emplace_back(std::move(candidate));
			}
		}
		return field;
	}
}

const Engine::ManagedScriptSchema& Engine::ManagedSchemaCache::Get(const std::string& scriptTypeID, bool initialized, const ManagedBridgeExports& bridge) {

	static const ManagedScriptSchema kEmpty{};

	if (scriptTypeID.empty()) {
		return kEmpty;
	}
	if (auto it = schemaCache_.find(scriptTypeID); it != schemaCache_.end()) {
		return it->second;
	}
	if (!initialized || !bridge.getScriptSchemaJsonSize_ || !bridge.copyScriptSchemaJson_) {
		return kEmpty;
	}

	// 二段階blobで必要サイズを取得してからvector確保してコピーする、固定長バッファを使わない
	int32_t size = 0;
	if (bridge.getScriptSchemaJsonSize_(scriptTypeID.c_str(), &size) != ManagedStatus::Ok || size <= 0) {
		return kEmpty;
	}
	std::string buffer(static_cast<size_t>(size), '\0');
	int32_t written = 0;
	if (bridge.copyScriptSchemaJson_(scriptTypeID.c_str(), buffer.data(), size, &written) != ManagedStatus::Ok) {
		return kEmpty;
	}
	buffer.resize(static_cast<size_t>(written));

	ManagedScriptSchema schema{};
	schema.scriptTypeID = scriptTypeID;
	try {
		nlohmann::json root = nlohmann::json::parse(buffer);
		schema.schemaVersion = root.value("schemaVersion", 0);
		schema.fullTypeName = root.value("fullTypeName", std::string{});
		if (root.contains("fields") && root["fields"].is_array()) {
			for (const auto& fieldNode : root["fields"]) {
				schema.fields.push_back(ParseFieldSchema(fieldNode));
			}
		}
	}
	catch (const nlohmann::json::exception& e) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptRuntime: Script Schemaを解析できません ScriptTypeID={} 内容={}",
			scriptTypeID, e.what());
	}

	auto [it, inserted] = schemaCache_.emplace(scriptTypeID, std::move(schema));
	return it->second;
}

void Engine::ManagedSchemaCache::Clear() {

	schemaCache_.clear();
}
