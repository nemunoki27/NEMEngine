#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Structures/ParticleSourceSettings.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderQueue.h>

// c++
#include <unordered_set>

namespace Engine {

	class GraphicsCore;
	class MeshRenderBackend;
	class AssetDatabase;
	class RenderAssetLibrary;
	class PipelineStateCache;

	struct ParticleMeshSourceReference {

		Entity renderer = Entity::Null();
		AssetID mesh{};
	};

	// RendererのMeshを優先し、参照が無い場合は保存Assetへ戻す
	ParticleMeshSourceReference ResolveParticleMeshSourceReference(const ECSWorld& world,
		Entity owner, const ParticleSourceSettings& settings);

	// 発生元が画面外でも同じSkinning経路で頂点を準備する
	class ParticleMeshSourcePreparation {
	public:
		void Prepare(GraphicsCore& graphics, ECSWorld& world, AssetDatabase& database,
			MeshRenderBackend& backend, RenderAssetLibrary& assets, PipelineStateCache& pipelines);
	private:
		RenderSceneBatch batch_;
		std::vector<AssetID> meshAssets_;
		std::vector<const RenderItem*> items_;
		std::unordered_set<uint64_t> renderers_;
	};
}
