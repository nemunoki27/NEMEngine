#include "ParticleMeshSourcePreparation.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderItemExtractor.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/Rendering/Particle/ParticleGPUState.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>

// c++
#include <algorithm>
#include <tuple>

using namespace Engine;

ParticleMeshSourceReference Engine::ResolveParticleMeshSourceReference(const ECSWorld& world,
	Entity owner, const ParticleSourceSettings& settings) {

	ParticleMeshSourceReference result;
	result.mesh = settings.mesh;
	const auto renderer = settings.renderer.Resolve(world, owner);
	const auto* component = world.TryGetComponent<MeshRendererComponent>(renderer);
	if (component) {
		result.renderer = renderer;
		if (component->mesh) result.mesh = component->mesh;
	}
	return result;
}

void ParticleMeshSourcePreparation::Prepare(GraphicsCore& graphics, ECSWorld& world, AssetDatabase& database,
	MeshRenderBackend& backend, RenderAssetLibrary& assets, PipelineStateCache& pipelines) {

	batch_.Clear();
	meshAssets_.clear();
	items_.clear();
	renderers_.clear();
	world.ForEach<ParticleSystemComponent>([&](const Entity& owner, const ParticleSystemComponent& component) {
		if (component.simulationTarget != ParticleSimulationTarget::GPU) return;
		const auto* runtime = TryGetParticleSystemRuntime(world, owner);
		const auto* state = runtime && runtime->instance.emitter.gpu ? runtime->instance.emitter.gpu.get() : nullptr;
		if (!state || state->rejected || !state->definition || !state->definition->settings) return;
		const auto& source = state->definition->settings->source;
		if (source.kind != ParticleSourceKind::MeshVertex && source.kind != ParticleSourceKind::MeshEdge &&
			source.kind != ParticleSourceKind::MeshSurface) return;
		const auto reference = ResolveParticleMeshSourceReference(world, owner, source);
		if (!reference.mesh) return;
		meshAssets_.push_back(reference.mesh);
		if (!reference.renderer.IsValid()) return;
		const uint64_t key = (uint64_t(reference.renderer.index) << 32) | reference.renderer.generation;
		if (!renderers_.insert(key).second) return;
		const auto& renderer = *world.TryGetComponent<MeshRendererComponent>(reference.renderer);
		RenderItem item;
		RenderItemExtract::FillCommonFields(item, world, reference.renderer, renderer,
			RenderItemExtract::GetWorldMatrix(world, reference.renderer));
		item.backendID = RenderBackendID::Mesh;
		item.material = renderer.material;
		item.cameraDomain = RenderCameraDomain::Perspective;
		// 描画バッチと分けてCameraやMaterialごとの再構築を避ける
		item.batchKey = 0x50415254534f5552ull;
		MeshRenderPayload payload;
		payload.mesh = reference.mesh;
		item.payload = batch_.PushPayload(payload);
		batch_.Add(std::move(item));
	});
	if (meshAssets_.empty()) return;
	const auto lessMesh = [](const AssetID& a, const AssetID& b) { return std::tie(a.high, a.low) < std::tie(b.high, b.low); };
	std::sort(meshAssets_.begin(), meshAssets_.end(), lessMesh);
	meshAssets_.erase(std::unique(meshAssets_.begin(), meshAssets_.end()), meshAssets_.end());
	backend.RequestMeshes(graphics, database, meshAssets_);
	batch_.SetSource(&world, world.GetRenderDataRevision(), world.GetRenderTransformRevision());
	for (const auto& item : batch_.GetItems()) items_.push_back(&item);
	std::sort(items_.begin(), items_.end(), [&](const auto* a, const auto* b) {
		return lessMesh(batch_.GetPayload<MeshRenderPayload>(*a)->mesh, batch_.GetPayload<MeshRenderPayload>(*b)->mesh);
	});
	// SkinningはCameraの可視判定とLODに依存しない入力を使う
	ResolvedRenderView view;
	view.valid = view.perspective.valid = true;
	view.width = view.height = 1;
	RenderBufferRegistry registry;
	RenderDrawContext context;
	context.graphicsCore = &graphics;
	context.view = &view;
	context.batch = &batch_;
	context.bufferRegistry = &registry;
	context.assetDatabase = &database;
	context.assetLibrary = &assets;
	context.pipelineCache = &pipelines;
	context.runtimeFeatures = graphics.GetDXObject().GetFeatureController().GetRuntimeFeatures();
	context.runtimeFeatures.useMeshLOD = false;
	context.runtimeFeatures.useFrustumCulling = false;
	context.runtimeFeatures.useOcclusionCulling = false;
	context.runtimeFeatures.useContributionCulling = false;
	context.runtimeFeatures.useNormalConeCulling = false;
	for (size_t begin = 0; begin < items_.size();) {
		const auto mesh = batch_.GetPayload<MeshRenderPayload>(*items_[begin])->mesh;
		size_t end = begin + 1;
		while (end < items_.size() && batch_.GetPayload<MeshRenderPayload>(*items_[end])->mesh == mesh) ++end;
		backend.PreDispatchSkinningBatch(context, std::span(items_).subspan(begin, end - begin));
		begin = end;
	}
}
