#include "MaterialParameterLookup.h"

const Engine::MaterialParameterValue* Engine::MaterialParameterLookup::Find(const MaterialParameterSet& parameters,
	MaterialParameterID id, MaterialParameterSemantic semantic, std::string_view name, const MaterialParameterSet* defaults) {

	if (const MaterialParameterValue* value = parameters.Find(id)) {
		return value;
	}
	if (semantic != MaterialParameterSemantic::None) {
		if (const MaterialParameterValue* value = parameters.Find(semantic)) {
			return value;
		}
	}
	if (const MaterialParameterValue* value = parameters.FindByName(name)) {
		return value;
	}
	if (defaults) {
		for (const MaterialParameterRecord& record : defaults->GetRecords()) {
			if (record.id == id) {
				return parameters.FindByName(record.namedValue.first);
			}
		}
	}
	return nullptr;
}
