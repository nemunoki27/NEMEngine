#include "ShaderGraphParameterDefaults.h"
#include <Engine/Core/Rendering/Materials/PaintMaterialAsset.h>

// c++
#include <algorithm>

//============================================================================
//	ShaderGraphParameterDefaults functions
//============================================================================
Engine::MaterialParameterSet Engine::ShaderGraphParameterDefaults::Build(const ShaderGraphAsset& graph) {

	MaterialParameterSet parameters;
	// 宣言したParameterの初期値を描画時の定数へ渡す
	for (const ShaderGraphParameter& parameter : graph.parameters) {
		parameters.Set(
			MaterialParameterID::FromUUID(parameter.id), parameter.name, parameter.semantic, parameter.defaultValue);
	}

	// 実行時に切り替えるKeywordを登録する
	for (const ShaderGraphKeyword& keyword : graph.keywords) {
		if (!keyword.runtimeToggle) {
			continue;
		}
		MaterialParameterValue value{};
		if (keyword.type == ShaderGraphKeywordType::Boolean) {
			value.value = keyword.defaultIndex != 0;
		} else {
			value.value = static_cast<int32_t>(keyword.defaultIndex);
		}
		parameters.Set(MaterialParameterID::FromUUID(keyword.id), keyword.name, MaterialParameterSemantic::None, value);
	}
	return parameters;
}

std::vector<Engine::MaterialParameterID> Engine::ShaderGraphParameterDefaults::CollectExposed(const ShaderGraphAsset& graph) {

	std::vector<MaterialParameterID> result;
	for (const auto& parameter : graph.parameters) {
		if (parameter.exposed && parameter.scope != ShaderGraphParameterScope::Global) {
			result.push_back(MaterialParameterID::FromUUID(parameter.id));
		}
	}
	for (const auto& keyword : graph.keywords) {
		if (keyword.runtimeToggle) result.push_back(MaterialParameterID::FromUUID(keyword.id));
	}
	return result;
}

void Engine::ShaderGraphParameterDefaults::ImportLegacyOverrides(const ShaderGraphAsset& graph, MaterialAsset& material) {

	const auto defaults = Build(graph);
	material.graphOverrides.clear();
	for (const auto id : CollectExposed(graph)) {
		const auto* value = material.parameters.Find(id);
		const auto* fallback = defaults.Find(id);
		if (value && fallback && value->value.index() == fallback->value.index() &&
			SerializeMaterialParameterValue(*value) != SerializeMaterialParameterValue(*fallback)) {
			material.graphOverrides.push_back(id);
		}
	}
}

void Engine::ShaderGraphParameterDefaults::RestoreOverrides(const ShaderGraphAsset& graph,
	const MaterialAsset& previous, MaterialAsset& material) {

	for (const auto& field : previous.parameters.GetRecords()) {
		if (IsPaintSettingParameter(field.namedValue.first) && field.id == MaterialParameterID::FromName(field.namedValue.first)) {
			material.parameters.Set(field.id, field.namedValue.first, field.semantic, field.namedValue.second);
		}
	}
	const auto exposed = CollectExposed(graph);
	for (const auto id : previous.graphOverrides) {
		if (std::ranges::find(exposed, id) == exposed.end()) continue;
		const auto* stored = previous.parameters.Find(id);
		const auto records = material.parameters.GetRecords();
		const auto field = std::ranges::find(records, id, &MaterialParameterRecord::id);
		if (!stored || field == records.end() || stored->value.index() != field->namedValue.second.value.index()) continue;
		const std::string name = field->namedValue.first;
		material.parameters.Set(id, name, field->semantic, *stored);
		material.graphOverrides.push_back(id);
	}
}

void Engine::ShaderGraphParameterDefaults::ApplyMissing(const MaterialParameterSet& defaults,
	MaterialParameterSet& parameters) {

	// 既存の編集値を保ち、未設定のIDだけを追加する
	for (const MaterialParameterRecord& record : defaults.GetRecords()) {
		if (!parameters.Find(record.id)) {
			parameters.Set(record.id, record.namedValue.first, record.semantic, record.namedValue.second);
		}
	}
}

void Engine::ShaderGraphParameterDefaults::ApplyToMaterial(const MaterialParameterSet& defaults,
	std::span<const ShaderParameterMetadata> metadata, MaterialParameterSet& parameters) {

	for (const MaterialParameterRecord& record : defaults.GetRecords()) {
		const auto field = std::find_if(metadata.begin(), metadata.end(),
			[&](const ShaderParameterMetadata& value) { return value.id == record.id; });
		const bool exposed = field != metadata.end() && field->exposed;
		if (!exposed || !parameters.Find(record.id)) {
			parameters.Set(record.id, record.namedValue.first, record.semantic, record.namedValue.second);
		}
	}
}
