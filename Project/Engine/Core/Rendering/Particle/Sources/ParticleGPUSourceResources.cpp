#include "ParticleGPUSourceResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>

// c++
#include <array>
#include <bit>

using namespace Engine;

ParticleGPUSourceResources::~ParticleGPUSourceResources() {

	if (emptyTexture_ != UINT32_MAX) descriptors_->Retire(emptyTexture_, {});
	if (emptyDepth_ != UINT32_MAX) descriptors_->Retire(emptyDepth_, {});
}

void ParticleGPUSourceResources::Init(GraphicsCore& graphics) {

	if (descriptors_) return;
	auto* device = graphics.GetDXObject().GetDevice();
	descriptors_ = &graphics.GetSRVDescriptor();
	retirement_ = &graphics.GetDXObject().GetResourceRetirement();
	emptyVertices_.Init(device, descriptors_);
	emptyIndices_.Init(device, descriptors_);
	emptyDistribution_.Init(device, descriptors_);
	emptyColors_.Init(device, descriptors_);
	subMeshes_.Init(device, descriptors_);
	previousVertices_.Init(device, descriptors_);
	D3D12_SHADER_RESOURCE_VIEW_DESC description{};
	description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	description.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	description.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	description.Texture2D.MipLevels = 1;
	descriptors_->CreateSRV(emptyTexture_, nullptr, description);
	description.Format = DXGI_FORMAT_R32_FLOAT;
	descriptors_->CreateSRV(emptyDepth_, nullptr, description);
	frameRegistration_.Bind(retirement_->GetFrameResources(), this, [](void* value, uint32_t count) {
		auto& self = *static_cast<ParticleGPUSourceResources*>(value);
		for (uint32_t i = count; i < kGraphicsFrameContextCount; ++i) self.owners_[i] = {};
	});
}

void ParticleGPUSourceResources::Retain(std::shared_ptr<const void> owner) {

	auto& frame = owners_[GraphicsFrameState::GetCurrentIndex()];
	const auto serial = GraphicsFrameState::GetFrameSerial();
	if (frame.serial != serial) { frame.values.clear(); frame.serial = serial; }
	if (owner) frame.values.push_back(std::move(owner));
}

bool ParticleGPUSourceResources::Prepare(GraphicsCore& graphics, ECSWorld& world, Entity owner,
	const ParticleGPUSimulationStep& step, RenderAssetLibrary& assets, PipelineStateCache& pipelines,
	const ParticleGPUFrameInputs& inputs, float deltaTime) {

	Init(graphics);
	Retain({});
	diagnostic_.clear();
	settings_ = {};
	settings_.emitterWorld = step.emitterWorld;
	settings_.previousWorld = previousEmitter_;
	const auto& source = step.definition->settings->source;
	settings_.kind = static_cast<uint32_t>(source.kind);
	settings_.stateSlot = step.program->sourceStateSlot;
	settings_.moduleStateCount = step.program->stateCount;
	settings_.textureSizeOffset = Vector4(source.textureSize.x, source.textureSize.y, source.surfaceOffset, source.inheritVelocity);
	settings_.maskRange = Vector4(source.maskMinimum, source.maskMaximum, static_cast<float>(source.maskChannel),
		std::bit_cast<float>(static_cast<uint32_t>(GraphicsFrameState::GetFrameSerial())));
	settings_.imageSize.z = deltaTime;
	settings_.flags = (source.sequential ? 1u : 0u) | (source.attachToSurface ? 2u : 0u) |
		(source.inheritColor ? 4u : 0u) | (source.useMask ? 8u : 0u) | (source.maskFromVertexColors ? 32u : 0u);
	// 空の入力にも有効なDescriptorを接続する
	const std::array<MeshVertex, 1> vertices{};
	const std::array<uint32_t, 1> indices{};
	const std::array<float, 1> distribution{1};
	const std::array<Vector4, 1> colors{Vector4(1, 1, 1, 1)};
	const std::array<ParticleSourceSubMeshGPU, 1> transforms{};
	emptyVertices_.Upload(vertices);
	emptyIndices_.Upload(indices);
	emptyDistribution_.Upload(distribution);
	emptyColors_.Upload(colors);
	subMeshes_.Upload(transforms);
	bindings_ = {emptyVertices_.GetGPUHandle(), emptyIndices_.GetGPUHandle(), emptyIndices_.GetGPUHandle(),
		subMeshes_.GetGPUHandle(), emptyDistribution_.GetGPUHandle(), emptyColors_.GetGPUHandle(), emptyVertices_.GetGPUHandle(),
		descriptors_->GetGPUHandle(emptyTexture_), descriptors_->GetGPUHandle(emptyDepth_),
		descriptors_->GetGPUHandle(emptyTexture_), descriptors_->GetGPUHandle(emptyDepth_)};
	currentVertices_ = nullptr;
	if (source.kind == ParticleSourceKind::Analytic) return true;
	if (source.useMask && !source.maskFromVertexColors) {
		const auto mask = RuntimeTextureResolver::ResolveBindless(graphics, assets.GetDatabase(), source.maskTexture, TextureColorSpace::Linear);
		if (mask.retry || mask.srvIndex == UINT32_MAX) { diagnostic_ = "発生マスクを読み込み中です"; return false; }
		bindings_[9] = descriptors_->GetGPUHandle(mask.srvIndex);
		Retain(mask.ownership);
	}
	const bool ready = source.kind <= ParticleSourceKind::MeshSurface ?
		PrepareMesh(graphics, world, owner, step, assets, pipelines, inputs) : PrepareImage(graphics, world, owner, source, assets, inputs);
	if (ready) previousEmitter_ = step.emitterWorld;
	return ready;
}

bool ParticleGPUSourceResources::PrepareImage(GraphicsCore& graphics, ECSWorld& world, Entity owner,
	const ParticleSourceSettings& source, RenderAssetLibrary& assets, const ParticleGPUFrameInputs& inputs) {

	if (source.kind == ParticleSourceKind::CameraSurface) {
		Entity camera = source.camera.localFileID ? source.camera.Resolve(world, owner) : inputs.mainCamera;
		const auto capture = inputs.resolveCamera ? inputs.resolveCamera(camera) : ParticleGPUCameraInput{};
		if (!capture.colorSRV.ptr || !capture.depthSRV.ptr || !capture.width || !capture.height) {
			diagnostic_ = "発生元Cameraの色と深度を準備中です";
			return false;
		}
		bindings_[7] = capture.colorSRV;
		bindings_[8] = capture.depthSRV;
		settings_.inverseCaptureVP = capture.inverseViewProjection;
		settings_.previousWorld = capture.previousInverseViewProjection;
		settings_.imageSize.x = static_cast<float>(capture.width);
		settings_.imageSize.y = static_cast<float>(capture.height);
		settings_.flags |= 128u;
		if (capture.previousDepthSRV.ptr) { settings_.flags |= 64u; bindings_[10] = capture.previousDepthSRV; }
		Retain(capture.owner);
	} else {
		const auto texture = RuntimeTextureResolver::ResolveBindless(graphics, assets.GetDatabase(), source.texture, TextureColorSpace::Auto);
		Vector2 size{};
		if (texture.retry || texture.srvIndex == UINT32_MAX ||
			!RuntimeTextureResolver::TryResolveSize(graphics, assets.GetDatabase(), source.texture, size)) {
			diagnostic_ = "発生元Textureを読み込み中です";
			return false;
		}
		bindings_[7] = descriptors_->GetGPUHandle(texture.srvIndex);
		settings_.imageSize.x = size.x;
		settings_.imageSize.y = size.y;
		settings_.flags |= previousReady_ ? 64u : 0u;
		previousReady_ = true;
		Retain(texture.ownership);
	}
	settings_.candidateCount = static_cast<uint32_t>(settings_.imageSize.x * settings_.imageSize.y);
	return settings_.candidateCount != 0;
}

void ParticleGPUSourceResources::Bind(GraphicsCore& graphics, const PipelineState& pipeline) {

	if (!pipeline.FindBindingByName("ParticleGPUSourceConstants", ShaderBindingKind::CBV)) return;
	auto* commands = graphics.GetDXObject().GetDxCommand()->GetCommandList();
	const auto allocation = constants_.AllocateAndUpload(*retirement_, graphics.GetDXObject().GetDevice(), settings_);
	RootBindingCommand::SetComputeCBV(commands, pipeline.FindBindingByName("ParticleGPUSourceConstants", ShaderBindingKind::CBV), allocation.gpuAddress);
	constexpr const char* names[]{"gGPUSourceVertices", "gGPUSourceIndices", "gGPUSourceVertexSubMeshes", "gGPUSourceSubMeshes",
		"gGPUSourceDistribution", "gGPUSourceVertexColors", "gGPUSourcePreviousVertices", "gGPUSourceColor", "gGPUSourceDepth",
		"gGPUSourceMask", "gGPUSourcePreviousDepth"};
	for (uint32_t i = 0; i < bindings_.size(); ++i) {
		if (const auto* binding = pipeline.FindBindingByName(names[i], ShaderBindingKind::SRV))
			RootBindingCommand::SetComputeSRV(commands, binding, 0, bindings_[i]);
	}
}

void ParticleGPUSourceResources::Finish(GraphicsCore& graphics) {

	if (!currentVertices_) return;
	auto& commands = *graphics.GetDXObject().GetDxCommand();
	previousVertices_.Transition(commands, D3D12_RESOURCE_STATE_COPY_DEST);
	if (currentSkinned_) commands.TransitionBarriers({currentVertices_}, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
	commands.GetCommandList()->CopyBufferRegion(previousVertices_.GetResource(), 0, currentVertices_, currentVertexOffset_,
		uint64_t(settings_.vertexCount) * sizeof(MeshVertex));
	if (currentSkinned_) commands.TransitionBarriers({currentVertices_}, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	previousVertices_.Transition(commands, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	previousReady_ = true;
}
