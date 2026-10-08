#include "ParticleEffectEditSession.h"
#include "ParticleEditorDescriptorRegistry.h"

using namespace Engine;

Engine::IParticleModule* ParticleEffectEditSession::ResolveModuleCache(ParticleModuleEditCacheEntry& cache, const ParticleEffectModuleEntry& entry) {

	// idが変わっていたら作り直し、現在のパラメータを読み込ませる
	if (!cache.module || cache.id != entry.id || cache.instanceID != entry.instanceID) {

		cache.instanceID = entry.instanceID;
		cache.id = entry.id;
		ParticleModuleRegistry& registry = ParticleModuleRegistry::GetInstance();
		cache.typeID = registry.FindTypeID(entry.id);
		cache.module = registry.Create(cache.typeID);
		cache.drawer = ParticleEditorDescriptorRegistry::GetInstance().CreateModuleDrawer(cache.typeID);
		if (cache.module) {
			cache.module->FromJson(entry.params);
		}
	}
	return cache.module.get();
}

