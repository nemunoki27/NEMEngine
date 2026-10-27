#include "ManagedScriptRuntime.h"

//============================================================================
//	include
//============================================================================
#include "ManagedScriptUtility.h"
#include "ManagedMaterialConversion.h"
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>

// c++
#include <cmath>

using namespace Engine;
using namespace Engine::ManagedMaterialConversion;

namespace {

	// 最初のゲーム更新でもAssetの参照先を解決する
	RenderAssetLibrary* ResolveLibrary() {

		const auto* context = ManagedScriptRuntime::GetCurrentContext();
		if (!context || !context->renderAssetLibrary || !context->assetDatabase) return nullptr;
		context->renderAssetLibrary->Init(context->assetDatabase);
		return context->renderAssetLibrary;
	}
}

//============================================================================
//	ManagedScriptRuntime assetSettingsMethods
//============================================================================
int32_t ManagedScriptRuntime::GetAssetRuntimeParameterCallback(ManagedAssetGUID assetID, uint64_t parameterID,
	ManagedMaterialParameterValue* output) {

	if (!output) return 0;
	try {
		auto* library = ResolveLibrary();
		const auto id = ToAssetID(assetID);
		const auto* meta = library ? library->GetDatabase()->Find(id) : nullptr;
		if (!meta) return 0;
		if (meta->type == AssetType::RenderTexture) {
			const auto* texture = library->LoadRenderTexture(id);
			if (!texture || (parameterID != MaterialParameterID::FromName("width").value &&
				parameterID != MaterialParameterID::FromName("height").value)) return 0;
			MaterialParameterValue value;
			value.value = static_cast<int32_t>(parameterID == MaterialParameterID::FromName("width").value ? texture->width : texture->height);
			return EncodeMaterialParameterValue(value, *output);
		}
		if (meta->type != AssetType::Material) return 0;
		const auto* material = library->LoadMaterial(id);
		const auto* value = material ? FindParameter(material->parameters, MaterialParameterID{parameterID}) : nullptr;
		return value && EncodeMaterialParameterValue(*value, *output);
	} catch (...) { return 0; }
}

int32_t ManagedScriptRuntime::SetAssetRuntimeParameterCallback(ManagedAssetGUID assetID, uint64_t parameterID,
	const char* name, const ManagedMaterialParameterValue* value) {

	if (!value || !name || !parameterID) return 0;
	try {
		auto* library = ResolveLibrary();
		const auto id = ToAssetID(assetID);
		const auto* meta = library ? library->GetDatabase()->Find(id) : nullptr;
		if (!meta) return 0;
		MaterialParameterValue decoded;
		if (!DecodeMaterialParameterValue(*value, decoded)) return 0;
		if (meta->type == AssetType::RenderTexture) {
			const auto* source = library->LoadRenderTexture(id);
			if (!source) return 0;
			auto texture = *source;
			if (parameterID == MaterialParameterID::FromName("size").value && std::holds_alternative<Vector2>(decoded.value)) {
				const auto size = std::get<Vector2>(decoded.value);
				if (!std::isfinite(size.x) || !std::isfinite(size.y) || size.x < 1 || size.x > 16384 || size.y < 1 || size.y > 16384) return 0;
				texture.width = static_cast<uint32_t>(size.x); texture.height = static_cast<uint32_t>(size.y);
			} else if (std::holds_alternative<int32_t>(decoded.value) && std::get<int32_t>(decoded.value) >= 1 && std::get<int32_t>(decoded.value) <= 16384) {
				if (parameterID == MaterialParameterID::FromName("width").value) texture.width = std::get<int32_t>(decoded.value);
				else if (parameterID == MaterialParameterID::FromName("height").value) texture.height = std::get<int32_t>(decoded.value);
				else return 0;
			} else return 0;
			library->SetRuntimeRenderTexture(texture);
			return 1;
		}
		if (meta->type != AssetType::Material) return 0;
		const auto* original = library->LoadMaterial(id);
		if (!original) return 0;
		auto material = *original;
		const auto resolved = ResolveParameterID(material.parameters, MaterialParameterID{parameterID});
		if (!IsParameterTypeCompatible(material.parameters.Find(resolved), decoded)) return 0;
		material.parameters.Set(resolved, name, ResolveMaterialParameterSemantic(name), decoded);
		library->SetRuntimeMaterial(std::move(material));
		if (const auto* context = GetCurrentContext(); context->world) context->world->MarkDataModified();
		return 1;
	} catch (...) { return 0; }
}

int32_t ManagedScriptRuntime::ClearAssetRuntimeParameterCallback(ManagedAssetGUID assetID, uint64_t parameterID) {

	try {
		auto* library = ResolveLibrary();
		const auto id = ToAssetID(assetID);
		const auto* original = library ? library->GetRuntimeMaterialBaseline(id) : nullptr;
		const auto* current = original ? library->LoadMaterial(id) : nullptr;
		if (!current) return 0;
		auto material = *current;
		const auto resolved = ResolveParameterID(material.parameters, MaterialParameterID{parameterID});
		material.parameters.erase(resolved);
		for (const auto& record : original->parameters.GetRecords()) {
			if (record.id == resolved) material.parameters.Set(record.id, record.namedValue.first, record.semantic, record.namedValue.second);
		}
		library->SetRuntimeMaterial(std::move(material));
		if (const auto* context = GetCurrentContext(); context->world) context->world->MarkDataModified();
		return 1;
	} catch (...) { return 0; }
}
