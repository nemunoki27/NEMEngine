#include "GraphSerializer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <filesystem>
#include <fstream>

//============================================================================
//	GraphSerializer classMethods
//============================================================================

namespace {

	nlohmann::json PinToJson(const Engine::GraphPin& pin) {

		// Enumは文字列で保存し、手編集時にも内容を追いやすくする
		nlohmann::json data = nlohmann::json::object();
		data["id"] = pin.id;
		data["nodeID"] = pin.nodeID;
		data["kind"] = Engine::EnumAdapter<Engine::GraphPinKind>::ToString(pin.kind);
		data["valueType"] = Engine::EnumAdapter<Engine::GraphValueType>::ToString(pin.valueType);
		data["name"] = pin.name;
		data["required"] = pin.required;
		data["allowMultipleLinks"] = pin.allowMultipleLinks;
		return data;
	}

	bool PinFromJson(const nlohmann::json& data, Engine::GraphPin& pin) {

		// PinはObject以外から復元しない
		if (!data.is_object()) {
			return false;
		}
		pin.id = data.value("id", 0ull);
		pin.nodeID = data.value("nodeID", 0ull);
		pin.kind = Engine::EnumAdapter<Engine::GraphPinKind>::FromString(
			data.value("kind", "Input")).value_or(Engine::GraphPinKind::Input);
		pin.valueType = Engine::EnumAdapter<Engine::GraphValueType>::FromString(
			data.value("valueType", "Unknown")).value_or(Engine::GraphValueType::Unknown);
		pin.name = data.value("name", "");
		pin.required = data.value("required", false);
		pin.allowMultipleLinks = data.value("allowMultipleLinks", false);
		return pin.id != 0;
	}

	nlohmann::json NodeToJson(const Engine::GraphNode& node) {

		// 汎用Node情報とTool固有Propertiesをまとめて保存する
		nlohmann::json data = nlohmann::json::object();
		data["id"] = node.id;
		data["type"] = node.type;
		data["displayName"] = node.displayName;
		data["position"] = { node.position.x, node.position.y };
		data["size"] = { node.size.x, node.size.y };
		data["enabled"] = node.enabled;
		data["collapsed"] = node.collapsed;
		data["properties"] = node.properties;

		data["inputs"] = nlohmann::json::array();
		for (const Engine::GraphPin& pin : node.inputs) {
			data["inputs"].push_back(PinToJson(pin));
		}
		data["outputs"] = nlohmann::json::array();
		for (const Engine::GraphPin& pin : node.outputs) {
			data["outputs"].push_back(PinToJson(pin));
		}
		return data;
	}

	bool NodeFromJson(const nlohmann::json& data, Engine::GraphNode& node) {

		// NodeはObject形式のみ受け付ける
		if (!data.is_object()) {
			return false;
		}

		// position / sizeは古いデータに無い場合があるため、存在確認してから読む
		node.id = data.value("id", 0ull);
		node.type = data.value("type", "");
		node.displayName = data.value("displayName", node.type);
		if (data.contains("position") && data["position"].is_array() && data["position"].size() >= 2) {
			node.position.x = data["position"][0].get<float>();
			node.position.y = data["position"][1].get<float>();
		}
		if (data.contains("size") && data["size"].is_array() && data["size"].size() >= 2) {
			node.size.x = data["size"][0].get<float>();
			node.size.y = data["size"][1].get<float>();
		}
		node.enabled = data.value("enabled", true);
		node.collapsed = data.value("collapsed", false);
		node.properties = data.value("properties", nlohmann::json::object());

		node.inputs.clear();
		if (data.contains("inputs") && data["inputs"].is_array()) {
			// 壊れたPinはスキップして、読み込める部分だけ復元する
			for (const auto& pinData : data["inputs"]) {
				Engine::GraphPin pin{};
				if (PinFromJson(pinData, pin)) {
					node.inputs.emplace_back(std::move(pin));
				}
			}
		}

		node.outputs.clear();
		if (data.contains("outputs") && data["outputs"].is_array()) {
			// OutputもInputと同じく、IDが有効なものだけ復元する
			for (const auto& pinData : data["outputs"]) {
				Engine::GraphPin pin{};
				if (PinFromJson(pinData, pin)) {
					node.outputs.emplace_back(std::move(pin));
				}
			}
		}
		return node.id != 0;
	}
}

nlohmann::json Engine::GraphSerializer::ToJson(const GraphDocument& document) {

	// DocumentのRoot情報
	nlohmann::json data = nlohmann::json::object();
	data["version"] = document.version;
	data["graphType"] = document.graphType;
	data["nextId"] = document.nextId;
	data["metadata"] = document.metadata;

	data["nodes"] = nlohmann::json::array();
	for (const GraphNode& node : document.nodes) {
		data["nodes"].push_back(NodeToJson(node));
	}

	// LinkはPin IDだけを保存し、Node復元後に同じIDで接続し直す
	data["links"] = nlohmann::json::array();
	for (const GraphLink& link : document.links) {
		nlohmann::json item = nlohmann::json::object();
		item["id"] = link.id;
		item["fromPinID"] = link.fromPinID;
		item["toPinID"] = link.toPinID;
		data["links"].push_back(item);
	}
	return data;
}

bool Engine::GraphSerializer::FromJson(const nlohmann::json& data, GraphDocument& document, std::string* error) {

	if (!data.is_object()) {
		if (error) {
			*error = "Graph json is not object.";
		}
		return false;
	}

	GraphDocument temp{};
	temp.version = data.value("version", 1u);
	temp.graphType = data.value("graphType", "");
	temp.nextId = data.value("nextId", 1ull);
	temp.metadata = data.value("metadata", nlohmann::json::object());

	if (data.contains("nodes") && data["nodes"].is_array()) {
		// Nodeは一旦tempへ復元し、最後にまとめて差し替える
		for (const auto& nodeData : data["nodes"]) {
			GraphNode node{};
			if (NodeFromJson(nodeData, node)) {
				temp.nodes.emplace_back(std::move(node));
			}
		}
	}

	if (data.contains("links") && data["links"].is_array()) {
		// LinkはPinの存在チェックをここでは行わず、Validator側で警告する
		for (const auto& linkData : data["links"]) {
			if (!linkData.is_object()) {
				continue;
			}
			GraphLink link{};
			link.id = linkData.value("id", 0ull);
			link.fromPinID = linkData.value("fromPinID", 0ull);
			link.toPinID = linkData.value("toPinID", 0ull);
			if (link.id != 0) {
				temp.links.emplace_back(link);
			}
		}
	}

	document = std::move(temp);
	return true;
}

bool Engine::GraphSerializer::Save(const std::string& path, const GraphDocument& document, std::string* error) {

	try {
		const std::filesystem::path filePath = path;
		if (filePath.has_parent_path()) {
			// Graph保存先フォルダが無い場合は作成する
			std::filesystem::create_directories(filePath.parent_path());
		}

		std::ofstream stream(filePath);
		if (!stream.is_open()) {
			if (error) {
				*error = "Failed to open graph file.";
			}
			return false;
		}
		stream << ToJson(document).dump(4);
		return true;
	} catch (const std::exception& e) {
		if (error) {
			*error = e.what();
		}
		return false;
	}
}

bool Engine::GraphSerializer::Load(const std::string& path, GraphDocument& document, std::string* error) {

	try {
		// JSONはGraphSerializer内で一括して検証しながら復元する
		std::ifstream stream(path);
		if (!stream.is_open()) {
			if (error) {
				*error = "Failed to open graph file.";
			}
			return false;
		}

		nlohmann::json data{};
		stream >> data;
		return FromJson(data, document, error);
	} catch (const std::exception& e) {
		if (error) {
			*error = e.what();
		}
		return false;
	}
}
