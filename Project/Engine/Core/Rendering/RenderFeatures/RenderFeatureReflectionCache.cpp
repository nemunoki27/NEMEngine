#include "RenderFeatureReflectionCache.h"

#include <algorithm>

void Engine::RenderFeatureReflectionCache::CacheReflection(
	AssetID materialID, MaterialPassKind passKind,
	const std::vector<ShaderConstantBufferVariable>& variables,
	const std::vector<ShaderResourceBinding>& resources,
	const std::vector<ShaderResourceBinding>& samplers) {

	const ReflectionKey key{ materialID, passKind };
	reflectionVariables_[key] = variables;
	reflectionResources_[key] = resources;
	reflectionSamplers_[key] = samplers;
}

void Engine::RenderFeatureReflectionCache::ClearReflection(
	AssetID materialID) {

	std::erase_if(reflectionVariables_,
		[materialID](const auto& entry) {

			return entry.first.material == materialID;
		});
	std::erase_if(reflectionResources_,
		[materialID](const auto& entry) {

			return entry.first.material == materialID;
		});
	std::erase_if(reflectionSamplers_,
		[materialID](const auto& entry) {

			return entry.first.material == materialID;
		});
}

void Engine::RenderFeatureReflectionCache::ClearReflectionCache() {

	reflectionVariables_.clear();
	reflectionResources_.clear();
	reflectionSamplers_.clear();
}

const std::vector<Engine::ShaderConstantBufferVariable>*
Engine::RenderFeatureReflectionCache::FindReflectionVariables(
	AssetID materialID, MaterialPassKind passKind) const {

	const auto found = reflectionVariables_.find(
		ReflectionKey{ materialID, passKind });
	return found == reflectionVariables_.end() ? nullptr : &found->second;
}

const std::vector<Engine::ShaderResourceBinding>*
Engine::RenderFeatureReflectionCache::FindReflectionResources(
	AssetID materialID, MaterialPassKind passKind) const {

	const auto found = reflectionResources_.find(
		ReflectionKey{ materialID, passKind });
	return found == reflectionResources_.end() ? nullptr : &found->second;
}

const std::vector<Engine::ShaderResourceBinding>*
Engine::RenderFeatureReflectionCache::FindReflectionSamplers(
	AssetID materialID, MaterialPassKind passKind) const {

	const auto found = reflectionSamplers_.find(
		ReflectionKey{ materialID, passKind });
	return found == reflectionSamplers_.end() ? nullptr : &found->second;
}

namespace Engine {

	size_t RenderFeatureReflectionCache::ReflectionKeyHash::operator()(const ReflectionKey& key) const noexcept {

		return std::hash<AssetID>{}(key.material) ^
			(std::hash<uint8_t>{}(
				static_cast<uint8_t>(key.passKind)) << 1);
	}
}
