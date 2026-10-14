#include "RenderFeatureReflectionCache.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>
#include <type_traits>
#include <utility>

//============================================================================
//	RenderFeatureReflectionCache classMethods
//============================================================================

void Engine::RenderFeatureReflectionCache::CacheReflection(AssetID materialID, MaterialPassKind passKind,
	const std::vector<ShaderConstantBufferVariable>& variables, const std::vector<ShaderResourceBinding>& resources,
	const std::vector<ShaderResourceBinding>& samplers, uint64_t pipelineID) {

	const ReflectionKey key{materialID, passKind};
	if (HasReflection(materialID, passKind, pipelineID)) return;
	// 全種類の取得後に同じ世代として公開する
	ReflectionData candidate{variables, resources, samplers, pipelineID};
	static_assert(std::is_nothrow_move_assignable_v<ReflectionData>);
	entries_.insert_or_assign(key, std::move(candidate));
}

bool Engine::RenderFeatureReflectionCache::HasReflection(AssetID materialID, MaterialPassKind passKind, uint64_t pipelineID) const {

	const auto found = entries_.find(ReflectionKey{materialID, passKind});
	return pipelineID != 0 && found != entries_.end() && found->second.pipelineID == pipelineID;
}

void Engine::RenderFeatureReflectionCache::ClearReflection(AssetID materialID) {

	// Materialに属する全用途の型情報を破棄する
	std::erase_if(entries_, [materialID](const auto& entry) { return entry.first.material == materialID; });
}

void Engine::RenderFeatureReflectionCache::ClearReflectionCache() {

	entries_.clear();
}

const std::vector<Engine::ShaderConstantBufferVariable>* Engine::RenderFeatureReflectionCache::FindReflectionVariables(
	AssetID materialID, MaterialPassKind passKind) const {

	const auto found = entries_.find(ReflectionKey{materialID, passKind});
	return found == entries_.end() ? nullptr : &found->second.variables;
}

const std::vector<Engine::ShaderResourceBinding>* Engine::RenderFeatureReflectionCache::FindReflectionResources(
	AssetID materialID, MaterialPassKind passKind) const {

	const auto found = entries_.find(ReflectionKey{materialID, passKind});
	return found == entries_.end() ? nullptr : &found->second.resources;
}

const std::vector<Engine::ShaderResourceBinding>* Engine::RenderFeatureReflectionCache::FindReflectionSamplers(
	AssetID materialID, MaterialPassKind passKind) const {

	const auto found = entries_.find(ReflectionKey{materialID, passKind});
	return found == entries_.end() ? nullptr : &found->second.samplers;
}

namespace Engine {

	size_t RenderFeatureReflectionCache::ReflectionKeyHash::operator()(const ReflectionKey& key) const noexcept {

		return std::hash<AssetID>{}(key.material) ^ (std::hash<uint8_t>{}(static_cast<uint8_t>(key.passKind)) << 1);
	}
}
