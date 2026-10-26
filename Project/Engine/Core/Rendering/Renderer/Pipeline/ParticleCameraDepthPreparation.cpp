#include "ParticleCameraDepthPreparation.h"

//============================================================================
//	include
//============================================================================
#include "RenderPipelineRunner.h"
#include "RenderPipelineUtility.h"
#include "RenderPassExecutionHelper.h"
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Views/CameraViewProjection.h>
#include <Engine/Core/Rendering/Renderer/Views/CameraViewSelection.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTargetCopyUtility.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Particle/ParticleGPUState.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>

// c++
#include <algorithm>
#include <unordered_set>

using namespace Engine;

struct ParticleCameraDepthPreparation::Data {

	struct Capture {

		std::unique_ptr<MultiRenderTarget> color;
		std::unique_ptr<DepthTexture2D> depth;
		Matrix4x4 inverseViewProjection = Matrix4x4::Identity();
		uint64_t serial = UINT64_MAX;
	};
	struct Camera {

		ResolvedRenderView view;
		std::unique_ptr<MultiRenderTarget> opaque;
		ParticleOpaqueDepthSnapshot depth;
		std::shared_ptr<Capture> latest;
		std::shared_ptr<Capture> previous;
		std::shared_ptr<Capture> input;
		std::shared_ptr<Capture> previousInput;
		std::vector<std::shared_ptr<Capture>> captures;
		bool depthNeeded = false;
		bool colorNeeded = false;
	};
	struct CaptureOwner {

		std::shared_ptr<const Capture> current;
		std::shared_ptr<const Capture> previous;
	};
	std::unordered_map<uint64_t, std::shared_ptr<Camera>> cameras;
	std::shared_ptr<const ECSWorldLifetime> worldLifetime;
	RenderPassPhaseBuckets buckets;
	std::vector<const RenderItem*> opaque;
	std::vector<AssetID> meshes;

	static uint64_t Key(Entity entity) { return (uint64_t(entity.index) << 32) | entity.generation; }

	ParticleGPUDepthInput ResolveDepth(Entity entity) const {

		const auto found = cameras.find(Key(entity));
		if (found == cameras.end()) return {};
		const auto& camera = found->second;
		auto* depth = camera->depth.GetTexture();
		if (!depth || camera->depth.GetFrameSerial() != GraphicsFrameState::GetFrameSerial()) return {};
		const auto& matrices = camera->view.perspective.matrices;
		return {depth->GetSRVGPUHandle(), matrices.viewProjectionMatrix,
			matrices.inverseProjectionMatrix * matrices.inverseViewMatrix, depth->GetWidth(), depth->GetHeight(), camera};
	}

	ParticleGPUCameraInput ResolveCamera(Entity entity) const {

		const auto found = cameras.find(Key(entity));
		if (found == cameras.end() || !found->second->input) return {};
		const auto& camera = *found->second;
		ParticleGPUCameraInput result;
		result.colorSRV = camera.input->color->GetColorTexture(0)->GetSRVGPUHandle();
		result.depthSRV = camera.input->depth->GetSRVGPUHandle();
		result.inverseViewProjection = camera.input->inverseViewProjection;
		result.width = camera.input->depth->GetWidth();
		result.height = camera.input->depth->GetHeight();
		if (camera.previousInput && camera.input->serial + 1 == GraphicsFrameState::GetFrameSerial() &&
			camera.previousInput->serial + 1 == camera.input->serial) {
			result.previousDepthSRV = camera.previousInput->depth->GetSRVGPUHandle();
			result.previousInverseViewProjection = camera.previousInput->inverseViewProjection;
		}
		result.owner = std::make_shared<CaptureOwner>(CaptureOwner{camera.input, camera.previousInput});
		return result;
	}
};

ParticleCameraDepthPreparation::ParticleCameraDepthPreparation() : data_(std::make_unique<Data>()) {}
ParticleCameraDepthPreparation::~ParticleCameraDepthPreparation() = default;

ParticleGPUFrameInputs ParticleCameraDepthPreparation::Prepare(GraphicsCore& graphics, const RenderFrameRequest& request,
	const SceneInstance* scene, std::span<const ResolvedRenderView> gameViews, Entity mainCamera, const RenderPipelineDeps& deps,
	const RenderBufferRegistry& sharedBuffers) {

	if (data_->worldLifetime != request.world->GetLifetime()) {
		data_->cameras.clear();
		data_->worldLifetime = request.world->GetLifetime();
	}
	std::erase_if(data_->cameras, [&](const auto& entry) {
		return !request.world->IsAlive(Entity{uint32_t(entry.first >> 32), uint32_t(entry.first)});
	});
	for (auto& [key, camera] : data_->cameras) {
		camera->depthNeeded = camera->colorNeeded = false;
		camera->input = camera->latest;
		camera->previousInput = camera->previous;
	}
	const auto* gameRequest = request.FindView(RenderViewKind::Game);
	const uint32_t width = gameRequest && gameRequest->width ? gameRequest->width : 1280u;
	const uint32_t height = gameRequest && gameRequest->height ? gameRequest->height : 720u;
	if (!request.world->IsAlive(mainCamera)) {
		mainCamera = CameraViewSelection::ResolveBestPerspectiveCamera(*request.world, width, height).sourceCamera;
	}
	const auto require = [&](Entity cameraEntity, bool depth, bool color) {
		if (!request.world->IsAlive(cameraEntity)) return;
		auto& camera = data_->cameras[Data::Key(cameraEntity)];
		if (!camera) camera = std::make_shared<Data::Camera>();
		camera->depthNeeded |= depth;
		camera->colorNeeded |= color;
	};
	request.world->ForEach<ParticleSystemComponent>([&](const Entity& owner, const ParticleSystemComponent& component) {
		if (component.simulationTarget != ParticleSimulationTarget::GPU) return;
		const auto* runtime = TryGetParticleSystemRuntime(*request.world, owner);
		const auto state = runtime ? runtime->instance.emitter.gpu : nullptr;
		if (!state || state->rejected || !state->definition || !state->program) return;
		if (state->program->hasCollisionDepthCamera) {
			const auto& reference = state->program->depthCamera;
			require(reference.localFileID ? reference.Resolve(*request.world, owner) : mainCamera, true, false);
		}
		const auto& source = state->definition->settings->source;
		if (source.kind == ParticleSourceKind::CameraSurface) {
			require(source.camera.localFileID ? source.camera.Resolve(*request.world, owner) : mainCamera, false, true);
		}
	});
	std::erase_if(data_->cameras, [](const auto& entry) { return !entry.second->depthNeeded && !entry.second->colorNeeded; });
	for (auto& [key, camera] : data_->cameras) {
		if (!camera->depthNeeded || !scene) continue;
		const Entity entity{uint32_t(key >> 32), uint32_t(key)};
		camera->view = {};
		for (const auto& view : gameViews) if (view.perspective.sourceCamera == entity) camera->view = view;
		if (!camera->view.valid) {
			auto* transform = request.world->TryGetComponent<TransformComponent>(entity);
			auto* component = request.world->TryGetComponent<PerspectiveCameraComponent>(entity);
			if (!transform || !component || !component->common.enabled) continue;
			camera->view.kind = RenderViewKind::Game;
			camera->view.width = width;
			camera->view.height = height;
			if (component->common.targetTexture) {
				const auto* output = deps.assetLibrary->LoadRenderTexture(component->common.targetTexture);
				if (output) { camera->view.width = output->width; camera->view.height = output->height; }
			}
			CameraViewProjection::UpdatePerspectiveCameraMatrices(*transform, *component, camera->view.width, camera->view.height);
			camera->view.perspective = CameraViewProjection::BuildFromPerspectiveCamera(entity, *transform, *component);
			camera->view.valid = camera->view.perspective.valid;
		}
		if (!camera->view.valid) continue;
		if (!camera->opaque || camera->opaque->GetWidth() != camera->view.width ||
			camera->opaque->GetHeight() != camera->view.height) {
			auto desc = RenderPathResources::BuildSceneMainDesc(camera->view.width, camera->view.height);
			// 深度計算だけに使う浮動小数点出力は最小の領域へ書く
			for (auto& color : desc.colors) {
				color.name = "ParticleDepth." + color.name;
				if (color.format == DXGI_FORMAT_R32G32B32A32_FLOAT || color.format == DXGI_FORMAT_R16G16B16A16_FLOAT)
					color.format = DXGI_FORMAT_R8G8B8A8_UNORM;
				if (color.format == DXGI_FORMAT_R16G16_FLOAT) color.format = DXGI_FORMAT_R8G8_UNORM;
			}
			desc.depth->debugName = L"ParticleCameraOpaqueDepth";
			auto target = std::make_unique<MultiRenderTarget>();
			target->Create(graphics.GetDXObject().GetDevice(), &graphics.GetRTVDescriptor(),
				&graphics.GetDSVDescriptor(), &graphics.GetSRVDescriptor(), desc);
			camera->opaque.swap(target);
		}
		SceneExecutionContext context;
		context.bufferRegistry = sharedBuffers;
		context.kind = RenderViewKind::Game;
		context.view = context.cullingView = context.billboardView = &camera->view;
		context.sceneInstance = scene;
		context.world = request.world;
		context.assetDatabase = request.assetDatabase;
		context.systemContext = request.systemContext;
		context.disableInlineRayTracing = true;
		context.forceVertexMeshVariant = true;
		context.disableLODDither = true;
		context.disableMeshCulling = true;
		RenderPassItemCollector::BuildBucketsForViewAndScene(*deps.renderBatch, camera->view, scene->instanceID, data_->buckets);
		data_->meshes.clear();
		data_->opaque.clear();
		for (const auto* item : data_->buckets.Get(RenderPhase::Opaque).items) {
			if (item->backendID != RenderBackendID::Mesh && item->backendID != RenderBackendID::Primitive) continue;
			data_->opaque.push_back(item);
			if (item->backendID == RenderBackendID::Mesh) {
				data_->meshes.push_back(deps.renderBatch->GetPayload<MeshRenderPayload>(*item)->mesh);
			}
		}
		if (auto* backend = dynamic_cast<MeshRenderBackend*>(deps.backendRegistry->Find(RenderBackendID::Mesh))) {
			backend->RequestMeshes(graphics, *request.assetDatabase, data_->meshes);
		}
		PreDispatchVisibleMeshSkinning(graphics, context, *deps.renderBatch, *deps.backendRegistry,
			*deps.assetLibrary, *deps.pipelineCache, *deps.materialResolver, data_->buckets);
		auto* target = camera->opaque.get();
		auto& commands = *graphics.GetDXObject().GetDxCommand();
		target->TransitionForRender(commands);
		target->Clear(commands, {.clearColor = true, .clearDepth = true});
		RenderPassExecutionHelper::Execute(graphics, context, data_->opaque, deps, target, MaterialPassKind::Draw, true, false);
		camera->depth.Copy(graphics, *target->GetDepthTexture());
	}
	ParticleGPUFrameInputs result;
	result.mainCamera = mainCamera;
	result.meshes = dynamic_cast<MeshRenderBackend*>(deps.backendRegistry->Find(RenderBackendID::Mesh));
	result.resolveDepth = [this](Entity camera) { return data_->ResolveDepth(camera); };
	result.resolveCamera = [this](Entity camera) { return data_->ResolveCamera(camera); };
	return result;
}

void ParticleCameraDepthPreparation::Capture(GraphicsCore& graphics, const SceneExecutionContext& context) {

	if (context.kind != RenderViewKind::Game || !context.view || !context.resources) return;
	const auto* camera = context.view->FindCamera(RenderCameraDomain::Perspective);
	if (!camera || !camera->sourceCamera.IsValid()) return;
	const auto found = data_->cameras.find(Data::Key(camera->sourceCamera));
	if (found == data_->cameras.end() || !found->second->colorNeeded) return;
	auto& state = *found->second;
	if (state.latest && state.latest->serial == GraphicsFrameState::GetFrameSerial()) return;
	auto* sourceColor = context.resources->GetSceneColorOpaque();
	auto& depthSnapshot = context.resources->GetParticleOpaqueDepthSnapshot();
	auto* sourceDepth = depthSnapshot.GetTexture();
	if (!sourceColor || !sourceDepth || depthSnapshot.GetFrameSerial() != GraphicsFrameState::GetFrameSerial()) return;
	const auto width = sourceDepth->GetWidth(), height = sourceDepth->GetHeight();
	const auto format = sourceColor->GetColorTexture(0)->GetFormat();
	std::erase_if(state.captures, [&](const auto& candidate) {
		return candidate.use_count() == 1 && candidate->color && (candidate->color->GetWidth() != width ||
			candidate->color->GetHeight() != height || candidate->color->GetColorTexture(0)->GetFormat() != format);
	});
	std::shared_ptr<Data::Capture> capture;
	for (const auto& candidate : state.captures) if (candidate.use_count() == 1) { capture = candidate; break; }
	if (!capture) {
		capture = std::make_shared<Data::Capture>();
		state.captures.push_back(capture);
	}
	if (!capture->color || capture->color->GetWidth() != width || capture->color->GetHeight() != height ||
		capture->color->GetColorTexture(0)->GetFormat() != format) {
		MultiRenderTargetCreateDesc colorDesc;
		colorDesc.width = width;
		colorDesc.height = height;
		colorDesc.colors.push_back({.name = "ParticleCameraColor", .format = format});
		auto color = std::make_unique<MultiRenderTarget>();
		color->Create(graphics.GetDXObject().GetDevice(), &graphics.GetRTVDescriptor(),
			&graphics.GetDSVDescriptor(), &graphics.GetSRVDescriptor(), colorDesc);
		DepthTextureCreateDesc depthDesc;
		depthDesc.width = width;
		depthDesc.height = height;
		depthDesc.resourceFormat = sourceDepth->GetResourceFormat();
		depthDesc.dsvFormat = sourceDepth->GetDSVFormat();
		depthDesc.srvFormat = sourceDepth->GetSRVFormat();
		depthDesc.debugName = L"ParticleCameraDepth";
		auto depth = std::make_unique<DepthTexture2D>();
		depth->Create(&graphics.GetDSVDescriptor(), &graphics.GetSRVDescriptor(), depthDesc);
		capture->color.swap(color);
		capture->depth.swap(depth);
	}
	MultiRenderTargetCopy::CopyColor0Resource(graphics, sourceColor, capture->color.get());
	auto& commands = *graphics.GetDXObject().GetDxCommand();
	sourceDepth->Transition(commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
	capture->depth->Transition(commands, D3D12_RESOURCE_STATE_COPY_DEST);
	commands.GetCommandList()->CopyResource(capture->depth->GetResource(), sourceDepth->GetResource());
	capture->depth->Transition(commands, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	sourceDepth->Transition(commands, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	capture->color->GetColorTexture(0)->Transition(commands,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	capture->inverseViewProjection = camera->matrices.inverseProjectionMatrix * camera->matrices.inverseViewMatrix;
	capture->serial = GraphicsFrameState::GetFrameSerial();
	state.previous = state.latest;
	state.latest = std::move(capture);
}

bool ParticleCameraDepthPreparation::NeedsCapture(Entity camera) const {

	const auto found = data_->cameras.find(Data::Key(camera));
	return found != data_->cameras.end() && found->second->colorNeeded;
}

void ParticleCameraDepthPreparation::Clear() {

	data_->cameras.clear();
	data_->worldLifetime.reset();
	data_->buckets.Clear();
	data_->opaque.clear();
	data_->meshes.clear();
}
