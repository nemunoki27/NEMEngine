#include "ParticleGPUFeedbackWorld.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/World/Components/Rendering/ParticlePaintReceiverComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>
#include <cmath>

using namespace Engine;

namespace {

	struct PaintConstants {

		ParticlePaintReceiverGPU receiver{};
		uint32_t resolution = 0;
		uint32_t tiles = 0;
		uint32_t operation = 0;
		uint32_t tileCapacity = 32;
	};
	static_assert(sizeof(PaintConstants) == 112);
}

void ParticleGPUFeedbackWorld::UpdatePaint(GraphicsCore& graphics, ECSWorld& world, const PipelineState& pipeline) {

	auto& platform = graphics.GetDXObject();
	auto& commands = *platform.GetDxCommand();
	auto* list = commands.GetCommandList();
	if (!emptyPaint_) {
		emptyPaint_ = std::make_unique<RenderTexture2D>();
		emptyPaint_->Create(platform.GetDevice(), &graphics.GetRTVDescriptor(), &graphics.GetSRVDescriptor(),
			{.width = 1, .height = 1, .format = DXGI_FORMAT_R16G16B16A16_FLOAT,
			.clearColor = Color4(0, 0, 0, 0), .createUAV = true, .debugName = L"ParticlePaintEmpty"});
	}
	emptyPaint_->Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	const std::array<ParticlePaintReceiverGPU, 1> emptyReceivers{};
	paintReceivers_.Upload(emptyReceivers);
	const uint32_t emptyCounts[]{0, 0, 0, 0};
	const auto emptyCountAllocation = constants_.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), emptyCounts);
	for (auto& [key, receiver] : receivers_) receiver->retained = false;
	uint64_t allocationBytes = 0;
	for (const auto& [key, receiver] : receivers_) allocationBytes += uint64_t(receiver->resolution) * receiver->resolution * 8;
	world.ForEach<ParticlePaintReceiverComponent, TransformComponent>([&](Entity entity,
		const ParticlePaintReceiverComponent& component, const TransformComponent& transform) {

		if (!component.enabled) return;
		if (!std::isfinite(component.planarSize.x) || !std::isfinite(component.planarSize.y) ||
			!std::isfinite(component.opacity) || component.planarSize.x <= 0 || component.planarSize.y <= 0) {
			diagnostic_ = "塗装の投影範囲と不透明度には有限の値を指定してください";
			return;
		}
		if (const auto* object = world.TryGetComponent<SceneObjectComponent>(entity); object && !object->activeInHierarchy) return;
		Matrix4x4 inverse;
		if (!Matrix4x4::TryInverse(transform.worldMatrix, inverse)) return;
		const uint32_t resolution = (std::clamp(component.resolution, 32u, 2048u) + 15u) & ~15u;
		const uint64_t key = (uint64_t(entity.generation) << 32) | entity.index;
		auto found = receivers_.find(key);
		if (found == receivers_.end()) {
			if (receivers_.size() >= 64 || allocationBytes + uint64_t(resolution) * resolution * 8 > 128ull * 1024 * 1024) {
				diagnostic_ = "塗装の受け取り面が64個またはTexture容量128MBの上限へ達しています";
				return;
			}
			auto receiver = std::make_unique<PaintReceiver>();
			receiver->tileCounts.Init(platform.GetDevice(), &graphics.GetSRVDescriptor());
			receiver->tileEvents.Init(platform.GetDevice(), &graphics.GetSRVDescriptor());
			found = receivers_.emplace(key, std::move(receiver)).first;
		}
		auto& receiver = *found->second;
		receiver.retained = true;
		if (!receiver.texture || receiver.resolution != resolution) {
			const uint64_t previousBytes = uint64_t(receiver.resolution) * receiver.resolution * 8;
			const uint64_t requestedBytes = uint64_t(resolution) * resolution * 8;
			if (allocationBytes - previousBytes + requestedBytes > 128ull * 1024 * 1024) {
				diagnostic_ = "塗装Texture容量128MBの上限へ達しています";
				return;
			}
			auto texture = std::make_unique<RenderTexture2D>();
			texture->Create(platform.GetDevice(), &graphics.GetRTVDescriptor(), &graphics.GetSRVDescriptor(),
				{.width = resolution, .height = resolution, .format = DXGI_FORMAT_R16G16B16A16_FLOAT,
				.clearColor = Color4(0, 0, 0, 0), .createUAV = true, .debugName = L"ParticlePaintReceiver"});
			receiver.texture = std::move(texture);
			allocationBytes = allocationBytes - previousBytes + requestedBytes;
			receiver.resolution = resolution;
			receiver.reset = true;
		}
		if (receiver.clearSerial != component.clearSerial) receiver.reset = true;
		receiver.clearSerial = component.clearSerial;
		receiver.data.worldToReceiver = inverse;
		receiver.data.entity = entity;
		receiver.data.texture = receiver.texture->GetSRVIndex();
		receiver.data.projection = static_cast<uint32_t>(component.projection);
		receiver.data.scaleOpacity = Vector4(1.0f / std::max(component.planarSize.x, 0.001f),
			1.0f / std::max(component.planarSize.y, 0.001f), std::clamp(component.opacity, 0.0f, 1.0f), 0);
		const uint32_t tiles = resolution / 16;
		receiver.tileCounts.EnsureCapacity(tiles * tiles);
		receiver.tileEvents.EnsureCapacity(tiles * tiles * 32);
		receiver.tileCounts.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		receiver.tileEvents.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		receiver.texture->Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		list->SetComputeRootSignature(pipeline.GetRootSignature());
		list->SetPipelineState(pipeline.GetComputePipeline());
		BindEvents(graphics, pipeline, nullptr, 0);
		if (const auto* binding = pipeline.FindBindingByName("gParticlePaintReceivers", ShaderBindingKind::SRV)) {
			RootBindingCommand::SetComputeSRV(list, binding, paintReceivers_.GetGPUAddress(), paintReceivers_.GetGPUHandle());
		}
		if (const auto* binding = pipeline.FindBindingByName("ParticlePaintSceneConstants", ShaderBindingKind::CBV)) {
			RootBindingCommand::SetComputeCBV(list, binding, emptyCountAllocation.gpuAddress);
		}
		if (const auto* binding = pipeline.FindBindingByName("gParticlePaintOutput", ShaderBindingKind::UAV)) {
			RootBindingCommand::SetComputeUAV(list, binding, 0, receiver.texture->GetUAVGPUHandle());
		}
		const auto bind = [&](const char* name, auto& buffer) {
			if (const auto* binding = pipeline.FindBindingByName(name, ShaderBindingKind::UAV)) {
				RootBindingCommand::SetComputeUAV(list, binding, buffer.GetGPUAddress(), buffer.GetUAVGPUHandle());
			}
		};
		bind("gParticlePaintTileCounts", receiver.tileCounts);
		bind("gParticlePaintTileEvents", receiver.tileEvents);
		PaintConstants data;
		data.receiver = receiver.data;
		data.resolution = resolution;
		data.tiles = tiles;
		const auto dispatch = [&](uint32_t operation, uint32_t count) {
			data.operation = operation;
			const auto allocation = constants_.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), data);
			if (const auto* binding = pipeline.FindBindingByName("ParticleGPUPaintConstants", ShaderBindingKind::CBV)) {
				RootBindingCommand::SetComputeCBV(list, binding, allocation.gpuAddress);
			}
			list->Dispatch((count + 255) / 256, 1, 1);
			commands.UAVBarrierAll();
		};
		if (receiver.reset) dispatch(3, resolution * resolution);
		receiver.reset = false;
		if (eventCapacity_) {
			dispatch(0, tiles * tiles);
			dispatch(1, eventCapacity_);
			dispatch(2, resolution * resolution);
		}
		receiver.texture->Transition(commands, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	});
	std::erase_if(receivers_, [](const auto& entry) { return !entry.second->retained; });
	std::vector<ParticlePaintReceiverGPU> data;
	for (const auto& [key, receiver] : receivers_) data.push_back(receiver->data);
	std::sort(data.begin(), data.end(), [](const auto& a, const auto& b) { return a.entity.index < b.entity.index; });
	receiverCount_ = static_cast<uint32_t>(data.size());
	if (data.empty()) data.emplace_back();
	paintReceivers_.Upload(data);
	if (eventCapacity_) {
		list->SetComputeRootSignature(pipeline.GetRootSignature());
		list->SetPipelineState(pipeline.GetComputePipeline());
		BindEvents(graphics, pipeline, nullptr, 0);
		if (const auto* binding = pipeline.FindBindingByName("gParticlePaintOutput", ShaderBindingKind::UAV)) {
			RootBindingCommand::SetComputeUAV(list, binding, 0, emptyPaint_->GetUAVGPUHandle());
		}
		if (const auto* binding = pipeline.FindBindingByName("gParticlePaintTileCounts", ShaderBindingKind::UAV)) {
			RootBindingCommand::SetComputeUAV(list, binding, counters_.GetGPUAddress(), counters_.GetUAVGPUHandle());
		}
		if (const auto* binding = pipeline.FindBindingByName("gParticlePaintTileEvents", ShaderBindingKind::UAV)) {
			RootBindingCommand::SetComputeUAV(list, binding, counters_.GetGPUAddress(), counters_.GetUAVGPUHandle());
		}
		if (const auto* binding = pipeline.FindBindingByName("gParticlePaintReceivers", ShaderBindingKind::SRV)) {
			RootBindingCommand::SetComputeSRV(list, binding, paintReceivers_.GetGPUAddress(), paintReceivers_.GetGPUHandle());
		}
		const uint32_t counts[]{receiverCount_, 0, 0, 0};
		const auto countAllocation = constants_.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), counts);
		if (const auto* binding = pipeline.FindBindingByName("ParticlePaintSceneConstants", ShaderBindingKind::CBV)) {
			RootBindingCommand::SetComputeCBV(list, binding, countAllocation.gpuAddress);
		}
		PaintConstants validation;
		validation.operation = 4;
		const auto allocation = constants_.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), validation);
		if (const auto* binding = pipeline.FindBindingByName("ParticleGPUPaintConstants", ShaderBindingKind::CBV)) {
			RootBindingCommand::SetComputeCBV(list, binding, allocation.gpuAddress);
		}
		list->Dispatch((eventCapacity_ + 255) / 256, 1, 1);
		commands.UAVBarrierAll();
	}
}
