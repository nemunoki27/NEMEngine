#include "ParticleGPUFeedbackWorld.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Decals/DecalCollector.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/World/Components/Rendering/DecalProjectorComponent.h>

// c++
#include <algorithm>
#include <utility>

using namespace Engine;

uint32_t ParticleGPUFeedbackWorld::PrepareDecalStyle(GraphicsCore& graphics, [[maybe_unused]] ECSWorld& world,
	RenderAssetLibrary& assets, const ParticleImpactDecalSettings& settings) {

	SceneExecutionContext context;
	context.assetDatabase = assets.GetDatabase();
	DecalProjectorComponent component;
	component.material = settings.material;
	component.color = settings.color;
	component.receiverRenderingLayerMask = settings.receiverRenderingLayerMask;
	DecalDrawItem item;
	if (!DecalCollector::ResolveMaterial(graphics, assets, context, component, 1.0f, item)) return UINT32_MAX;
	item.gpu.tangentX.w = settings.size.x;
	item.gpu.tangentY.w = settings.size.y;
	item.gpu.direction.w = settings.size.z;
	const uint32_t requested = std::clamp(settings.capacity, 1u, ParticleFeedbackSettings::kMaxDecalCapacity);
	if (requested > decalCapacity_) {
		decalCapacity_ = requested;
		resetDecals_ = true;
	}
	const std::string key(reinterpret_cast<const char*>(&item.gpu), sizeof(item.gpu));
	const auto found = styleLookup_.find(key);
	if (found != styleLookup_.end()) return found->second;
	if (styleData_.size() == 256) return UINT32_MAX;
	const uint32_t index = static_cast<uint32_t>(styleData_.size());
	styleLookup_.emplace(key, index);
	styleData_.push_back(item.gpu);
	styleTextures_.push_back(std::move(item.textures));
	return index;
}

void ParticleGPUFeedbackWorld::UpdateDecals(GraphicsCore& graphics, [[maybe_unused]] ECSWorld& world,
	[[maybe_unused]] RenderAssetLibrary& assets, const PipelineState& pipeline, float deltaTime) {

	auto& platform = graphics.GetDXObject();
	auto& commands = *platform.GetDxCommand();
	auto* list = commands.GetCommandList();
	ComPtr<ID3D12Resource> previous = decals_.GetResource();
	decals_.EnsureCapacity(std::max(1u, decalCapacity_));
	if (allocatedDecalCapacity_ && previous.Get() != decals_.GetResource()) {
		const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(previous.Get(),
			D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
		list->ResourceBarrier(1, &barrier);
		decals_.Transition(commands, D3D12_RESOURCE_STATE_COPY_DEST);
		list->CopyBufferRegion(decals_.GetResource(), 0, previous.Get(), 0, uint64_t(allocatedDecalCapacity_) * sizeof(ParticleImpactDecalGPU));
	}
	decalFreeList_.EnsureCapacity(decalCapacity_ + 1);
	decalArguments_.EnsureCapacity(1);
	const uint32_t wordCount = (decalCapacity_ + 31) / 32;
	const uint32_t summaryCount = (wordCount + 31) / 32;
	decalBins_.EnsureCapacity(std::max(1u, 257 * wordCount));
	decalBinWords_.EnsureCapacity(std::max(1u, 257 * summaryCount));
	decals_.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	decalFreeList_.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	decalArguments_.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	decalBins_.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	decalBinWords_.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	const std::array<DecalGPUData, 1> empty{};
	decalStyles_.Upload(styleData_.empty() ? std::span<const DecalGPUData>(empty) : styleData_);
	list->SetComputeRootSignature(pipeline.GetRootSignature());
	list->SetPipelineState(pipeline.GetComputePipeline());
	BindEvents(graphics, pipeline, nullptr, 0);
	if (const auto* binding = pipeline.FindBindingByName("gGPUImpactDecalStyles", ShaderBindingKind::SRV)) {
		RootBindingCommand::SetComputeSRV(list, binding, decalStyles_.GetGPUAddress(), decalStyles_.GetGPUHandle());
	}
	const auto bind = [&](const char* name, auto& buffer) {
		if (const auto* binding = pipeline.FindBindingByName(name, ShaderBindingKind::UAV)) {
			RootBindingCommand::SetComputeUAV(list, binding, buffer.GetGPUAddress(), buffer.GetUAVGPUHandle());
		}
	};
	bind("gGPUImpactDecalsOut", decals_);
	bind("gGPUImpactDecalFreeList", decalFreeList_);
	bind("gGPUImpactDecalArguments", decalArguments_);
	bind("gGPUImpactDecalBinsOut", decalBins_);
	bind("gGPUImpactDecalBinWordsOut", decalBinWords_);
	struct Constants {

		uint32_t capacity;
		uint32_t styleCount;
		uint32_t operation;
		float deltaTime;
		uint32_t previousCapacity;
		uint32_t wordCount;
		uint32_t summaryCount;
		uint32_t reserved;
	};
	Constants data{decalCapacity_, static_cast<uint32_t>(styleData_.size()), 0, std::max(0.0f, deltaTime), allocatedDecalCapacity_, wordCount, summaryCount, 0};
	const auto dispatch = [&](uint32_t operation, uint32_t count) {
		data.operation = operation;
		const auto allocation = constants_.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), data);
		if (const auto* binding = pipeline.FindBindingByName("ParticleGPUImpactDecalConstants", ShaderBindingKind::CBV)) {
			RootBindingCommand::SetComputeCBV(list, binding, allocation.gpuAddress);
		}
		list->Dispatch((std::max(1u, count) + 255) / 256, 1, 1);
		commands.UAVBarrierAll();
	};
	if (resetDecals_) {
		dispatch(6, 1);
		dispatch(5, decalCapacity_);
	}
	resetDecals_ = false;
	allocatedDecalCapacity_ = decalCapacity_;
	dispatch(1, decalCapacity_);
	if (eventCapacity_ && decalCapacity_) dispatch(2, eventCapacity_);
	dispatch(3, 1);
	dispatch(4, decalCapacity_);
	// 重なる候補を固定の順序で辿れるbit列へ登録する
	dispatch(7, std::max(257 * wordCount, 257 * summaryCount));
	dispatch(8, decalCapacity_);
	decalBins_.Transition(commands, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	decalBinWords_.Transition(commands, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	decals_.Transition(commands, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	decalArguments_.Transition(commands, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
}
