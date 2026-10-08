#include "ShaderGraphParameterDefaults.h"

//============================================================================
//	ShaderGraphParameterDefaults functions
//============================================================================
Engine::MaterialParameterSet Engine::ShaderGraphParameterDefaults::Build(const ShaderGraphAsset& graph) {

	MaterialParameterSet parameters;
	// 公開Parameterの初期値を登録する
	for (const ShaderGraphParameter& parameter : graph.parameters) {
		if (!parameter.exposed || parameter.scope == ShaderGraphParameterScope::Global) {
			continue;
		}
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

void Engine::ShaderGraphParameterDefaults::ApplyMissing(const MaterialParameterSet& defaults,
	MaterialParameterSet& parameters) {

	// 既存の編集値を保ち、未設定のIDだけを追加する
	for (const MaterialParameterRecord& record : defaults.GetRecords()) {
		if (!parameters.Find(record.id)) {
			parameters.Set(record.id, record.namedValue.first, record.semantic, record.namedValue.second);
		}
	}
}
