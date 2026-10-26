#include "ParticleGPUFeedbackWorld.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>
#include <stdexcept>

using namespace Engine;

void ParticleGPUFeedbackWorld::Initialize(GraphicsCore& graphics) {

	if (initialized_) return;
	auto* device = graphics.GetDXObject().GetDevice();
	auto* descriptors = &graphics.GetSRVDescriptor();
	for (uint32_t index = 0; index < 2; ++index) {
		events_[index].Init(device, descriptors);
		eventCounts_[index].Init(device, descriptors);
		events_[index].EnsureCapacity(1);
		eventCounts_[index].EnsureCapacity(1);
	}
	counters_.Init(device, descriptors);
	counters_.EnsureCapacity(8);
	emptyChildren_.Init(device, descriptors);
	emptyChildren_.EnsureCapacity(1);
	emptyChildCounts_.Init(device, descriptors);
	emptyChildCounts_.EnsureCapacity(1);
	emptyInheritedStates_.Init(device, descriptors);
	emptyInheritedStates_.EnsureCapacity(1);
	systems_.Init(device, descriptors);
	emptyRoutes_.Init(device, descriptors);
	emptyEvents_.Init(device, descriptors);
	emptyCounts_.Init(device, descriptors);
	paintReceivers_.Init(device, descriptors);
	const std::array<ParticlePaintReceiverGPU, 1> emptyReceivers{};
	paintReceivers_.Upload(emptyReceivers);
	decalStyles_.Init(device, descriptors);
	decals_.Init(device, descriptors);
	decals_.EnsureCapacity(1);
	decalFreeList_.Init(device, descriptors);
	decalFreeList_.EnsureCapacity(2);
	decalArguments_.Init(device, descriptors);
	decalArguments_.EnsureCapacity(1);
	decalBins_.Init(device, descriptors);
	decalBins_.EnsureCapacity(1);
	decalBinWords_.Init(device, descriptors);
	decalBinWords_.EnsureCapacity(1);
	for (auto& [state, source] : sources_) {
		source->routes.Init(device, descriptors);
		source->children.Init(device, descriptors);
		source->childCounts.Init(device, descriptors);
		source->children.EnsureCapacity(1);
		source->childCounts.EnsureCapacity(1);
	}
	for (auto& readback : readbacks_) readback.buffer.CreateBuffer(graphics.GetDXObject().GetResourceRetirement(), device);
	initialized_ = true;
}

void ParticleGPUFeedbackWorld::PrepareDefaults(GraphicsCore& graphics) {

	Initialize(graphics);
	const uint64_t serial = GraphicsFrameState::GetFrameSerial();
	if (constantFrameSerial_ != serial) {
		constants_.BeginFrame();
		constantFrameSerial_ = serial;
	}
	if (defaultsFrameSerial_ == serial) return;
	const std::array<ParticleGPUEventRoute, 1> routes{};
	const std::array<ParticleGPUEvent, 1> events{};
	const std::array<uint32_t, 1> counts{};
	emptyRoutes_.Upload(routes);
	emptyEvents_.Upload(events);
	emptyCounts_.Upload(counts);
	const std::array<ParticleGPUIdentity, 1> identities{};
	systems_.Upload(identityData_.empty() ? std::span<const ParticleGPUIdentity>(identities) : identityData_);
	defaultsFrameSerial_ = serial;
}

void ParticleGPUFeedbackWorld::BeginUpdate(GraphicsCore& graphics, ECSWorld& world,
	RenderAssetLibrary& assets, const PipelineState& pipeline) {

	const bool firstUpdate = !eventsInitialized_;
	PrepareDefaults(graphics);
	eventsInitialized_ = true;
	CollectStatistics(graphics);
	diagnostic_.clear();
	if (constantFrameSerial_ != GraphicsFrameState::GetFrameSerial()) {
		constants_.BeginFrame();
		constantFrameSerial_ = GraphicsFrameState::GetFrameSerial();
	}
	std::erase_if(sources_, [](const auto& entry) { return entry.second->owner.expired(); });
	for (auto& [state, source] : sources_) {
		source->active = source->prepared = source->incoming = source->acceptsChildren = false;
		source->routeData.clear();
	}
	std::vector<Entity> entities;
	std::vector<Source*> sources;
	std::vector<std::vector<Entity>> targets;
	uint32_t requiredCapacity = 0;
	decalEmissionPossible_ = false;
	world.ForEach<ParticleSystemComponent, ParticleSystemRuntimeComponent>([&](Entity entity,
		const ParticleSystemComponent& component, const ParticleSystemRuntimeComponent&) {
		if (!component.enabled) return;
		auto* runtime = TryGetParticleSystemRuntime(world, entity);
		if (!runtime || !runtime->instance.emitter.gpu || !runtime->playing || runtime->paused) return;
		auto state = runtime->instance.emitter.gpu;
		if (state->rejected || !state->definition || !state->program) return;
		const bool advances = std::any_of(state->pending.begin(), state->pending.end(), [](const auto& step) {
			return step.deltaTime > 0 || step.spawnCount != 0;
		});
		if (!advances) return;
		if (const auto* object = world.TryGetComponent<SceneObjectComponent>(entity); object && !object->activeInHierarchy) return;
		auto& slot = sources_[state.get()];
		if (!slot) {
			slot = std::make_unique<Source>();
			slot->owner = state;
			slot->identity.token = nextToken_++;
			slot->routes.Init(graphics.GetDXObject().GetDevice(), &graphics.GetSRVDescriptor());
			slot->children.Init(graphics.GetDXObject().GetDevice(), &graphics.GetSRVDescriptor());
			slot->childCounts.Init(graphics.GetDXObject().GetDevice(), &graphics.GetSRVDescriptor());
			slot->inheritedStates.Init(graphics.GetDXObject().GetDevice(), &graphics.GetSRVDescriptor());
			slot->children.EnsureCapacity(1);
			slot->childCounts.EnsureCapacity(1);
		}
		auto& source = *slot;
		source.identity.entity = entity;
		source.identity.epoch = state->epoch;
		source.active = true;
		source.acceptsChildren = !runtime->instance.emissionStopped;
		source.childrenConsumed = false;
		source.diagnostic.clear();
		entities.push_back(entity);
		sources.push_back(&source);
		targets.emplace_back();
		const auto& settings = state->definition->settings->feedback;
		if (settings.decal.enabled) {
			for (const auto& step : state->pending) decalEmissionPossible_ |= step.deltaTime > 0;
		}
		if (!settings.events.empty() || settings.paint.enabled || settings.decal.enabled) {
			requiredCapacity = std::min(ParticleFeedbackSettings::kMaxEventCapacity, requiredCapacity + settings.eventCapacity);
		}
	});
	for (size_t index = 0; index < sources.size(); ++index) {
		auto& source = *sources[index];
		const auto state = source.owner.lock();
		const auto& feedback = state->definition->settings->feedback;
		for (const auto& event : feedback.events) {
			if (!event.enabled) continue;
			const Entity target = event.target.Resolve(world, source.identity.entity);
			const auto found = std::find(entities.begin(), entities.end(), target);
			if (found == entities.end()) continue;
			auto& destination = *sources[std::distance(entities.begin(), found)];
			if (!destination.acceptsChildren) continue;
			destination.incoming = true;
			targets[index].push_back(target);
			ParticleGPUEventRoute route;
			route.target = destination.identity;
			route.kind = static_cast<uint32_t>(event.kind);
			route.phase = event.phase;
			route.count = event.count;
			route.probability = event.probability;
			route.time = event.time;
			route.flags = (event.inheritPosition ? 1u : 0u) | (event.inheritNormal ? 2u : 0u) |
				(event.inheritVelocity ? 4u : 0u) | (event.inheritColor ? 8u : 0u) | (event.inheritSeed ? 16u : 0u);
			source.routeData.push_back(route);
		}
		if (feedback.paint.enabled) {
			auto& route = source.routeData.emplace_back();
			route.flags = 64u;
			route.parameters = Vector4(feedback.paint.radius, feedback.paint.hardness, 0, 0);
			route.color = Vector4(feedback.paint.color.r, feedback.paint.color.g, feedback.paint.color.b, feedback.paint.color.a);
		}
		if (feedback.decal.enabled) {
			const uint32_t style = PrepareDecalStyle(graphics, world, assets, feedback.decal);
			if (style != UINT32_MAX) {
				auto& route = source.routeData.emplace_back();
				route.flags = 128u | (style << 16u);
				route.parameters = Vector4(0, feedback.decal.fadeDuration, feedback.decal.lifetime, 0);
			}
			else source.diagnostic = "GPUデカールのMaterialが未完了かStyle上限へ達しています";
		}
	}
	if (HasParticleEventCycle(entities, targets)) {
		for (Source* source : sources) {
			std::erase_if(source->routeData, [](const auto& route) { return (route.flags & 192u) == 0; });
			source->incoming = false;
			source->diagnostic = "GPUイベントの発生経路が循環しています";
		}
	}
	std::vector<ParticleGPUIdentity> identities;
	for (const Source* source : sources) identities.push_back(source->identity);
	std::sort(identities.begin(), identities.end(), [](const auto& a, const auto& b) { return a.entity.index < b.entity.index; });
	systemCount_ = static_cast<uint32_t>(identities.size());
	identityData_ = identities;
	if (identities.empty()) identities.emplace_back();
	systems_.Upload(identities);
	const bool reset = firstUpdate || eventCapacity_ != requiredCapacity;
	eventCapacity_ = requiredCapacity;
	writeIndex_ ^= 1u;
	auto& commands = *graphics.GetDXObject().GetDxCommand();
	for (uint32_t index = 0; index < 2; ++index) {
		events_[index].EnsureCapacity(std::max(1u, eventCapacity_));
		events_[index].Transition(commands, reset || index == writeIndex_ ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
		eventCounts_[index].Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	}
	counters_.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	emptyChildren_.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	emptyInheritedStates_.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	emptyChildCounts_.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	commands.GetCommandList()->SetComputeRootSignature(pipeline.GetRootSignature());
	commands.GetCommandList()->SetPipelineState(pipeline.GetComputePipeline());
	const auto dispatch = [&](uint32_t operation) {
		BindEvents(graphics, pipeline, nullptr, operation);
		commands.GetCommandList()->Dispatch(1, 1, 1);
		commands.UAVBarrierAll();
	};
	if (firstUpdate) dispatch(3);
	if (reset) {
		dispatch(0);
		writeIndex_ ^= 1u;
		dispatch(0);
		writeIndex_ ^= 1u;
	}
	dispatch(0);
	dispatch(1);
	events_[writeIndex_ ^ 1u].Transition(commands, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	eventCounts_[writeIndex_ ^ 1u].Transition(commands, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	emptyChildCounts_.Transition(commands, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	emptyChildren_.Transition(commands, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	for (Source* source : sources) {
		const auto state = source->owner.lock();
		source->childCapacity = source->incoming ? state->definition->settings->emitter.maxParticles : 0;
		if (source->childCapacity) source->children.EnsureCapacity(source->childCapacity);
		const uint32_t feedbackCapacity = source->incoming || !source->routeData.empty() ?
			state->definition->settings->emitter.maxParticles : 0;
		if (feedbackCapacity) {
			source->resetInherited |= feedbackCapacity > source->inheritedCapacity;
			source->inheritedCapacity = std::max(source->inheritedCapacity, feedbackCapacity);
			source->inheritedStates.EnsureCapacity(source->inheritedCapacity);
		}
		if (source->inheritedCapacity) source->inheritedStates.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		const std::array<ParticleGPUEventRoute, 1> empty{};
		source->routes.Upload(source->routeData.empty() ? std::span<const ParticleGPUEventRoute>(empty) : source->routeData);
	}
}

void ParticleGPUFeedbackWorld::BindEvents(GraphicsCore& graphics, const PipelineState& pipeline, Source* source, uint32_t operation, bool allowEvents) {

	PrepareDefaults(graphics);
	auto& platform = graphics.GetDXObject();
	auto* commands = platform.GetDxCommand()->GetCommandList();
	auto& command = *platform.GetDxCommand();
	events_[writeIndex_].Transition(command, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	eventCounts_[writeIndex_].Transition(command, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	counters_.Transition(command, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	const bool prepared = allowEvents && source && source->prepared && source->preparedFrame == GraphicsFrameState::GetFrameSerial();
	ParticleGPUEventConstants data;
	if (source) {
		data.source = source->identity;
		data.routeCount = prepared ? static_cast<uint32_t>(source->routeData.size()) : 0;
		data.childCapacity = operation == 4 ? source->inheritedCapacity :
			prepared && !source->childrenConsumed ? source->childCapacity : 0;
		data.reserved = source->inheritedCapacity ? 1u : 0u;
	}
	data.systemCount = systemCount_;
	data.eventCapacity = eventCapacity_;
	data.operation = operation;
	const auto allocation = constants_.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), data);
	if (const auto* binding = pipeline.FindBindingByName("ParticleGPUEventConstants", ShaderBindingKind::CBV)) {
		RootBindingCommand::SetComputeCBV(commands, binding, allocation.gpuAddress);
	}
	const auto srv = [&](const char* name, const auto& buffer, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
		if (const auto* binding = pipeline.FindBindingByName(name, ShaderBindingKind::SRV)) {
			RootBindingCommand::SetComputeSRV(commands, binding, buffer.GetGPUAddress(), handle);
		}
	};
	const auto uav = [&](const char* name, auto& buffer) {
		if (const auto* binding = pipeline.FindBindingByName(name, ShaderBindingKind::UAV)) {
			RootBindingCommand::SetComputeUAV(commands, binding, buffer.GetGPUAddress(), buffer.GetUAVGPUHandle());
		}
	};
	srv("gGPUEventSystems", systems_, systems_.GetGPUHandle());
	if (operation == 2) {
		srv("gGPUPreviousEvents", events_[writeIndex_ ^ 1u], events_[writeIndex_ ^ 1u].GetSRVGPUHandle());
		srv("gGPUPreviousEventCounts", eventCounts_[writeIndex_ ^ 1u], eventCounts_[writeIndex_ ^ 1u].GetSRVGPUHandle());
	} else {
		srv("gGPUPreviousEvents", emptyEvents_, emptyEvents_.GetGPUHandle());
		srv("gGPUPreviousEventCounts", emptyCounts_, emptyCounts_.GetGPUHandle());
	}
	if (prepared) srv("gGPUEventRoutes", source->routes, source->routes.GetGPUHandle());
	else srv("gGPUEventRoutes", emptyRoutes_, emptyRoutes_.GetGPUHandle());
	auto& children = prepared && source->childCapacity ? source->children : emptyChildren_;
	auto& counts = prepared && source->childCapacity ? source->childCounts : emptyChildCounts_;
	if (pipeline.FindBindingByName("gGPUChildrenOut", ShaderBindingKind::UAV)) {
		children.Transition(command, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		counts.Transition(command, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	} else {
		children.Transition(command, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
		counts.Transition(command, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	}
	srv("gGPUChildren", children, children.GetSRVGPUHandle());
	srv("gGPUChildCounts", counts, counts.GetSRVGPUHandle());
	uav("gGPUEvents", events_[writeIndex_]);
	uav("gGPUEventCounts", eventCounts_[writeIndex_]);
	uav("gGPUChildrenOut", children);
	uav("gGPUChildCountsOut", counts);
	uav("gGPUFeedbackStatistics", counters_);
	auto& inherited = source && source->inheritedCapacity ? source->inheritedStates : emptyInheritedStates_;
	inherited.Transition(command, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	uav("gGPUInheritedStates", inherited);
}

void ParticleGPUFeedbackWorld::PrepareSource(GraphicsCore& graphics, ParticleGPUState& state, const PipelineState& pipeline) {

	const auto found = sources_.find(&state);
	if (found == sources_.end()) return;
	auto& source = *found->second;
	if (!source.active || source.prepared) return;
	source.prepared = true;
	source.preparedFrame = GraphicsFrameState::GetFrameSerial();
	auto& command = *graphics.GetDXObject().GetDxCommand();
	auto* list = command.GetCommandList();
	if (source.childCapacity) {
		source.childCounts.Transition(command, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		source.children.Transition(command, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	}
	if (source.resetInherited) {
		list->SetComputeRootSignature(pipeline.GetRootSignature());
		list->SetPipelineState(pipeline.GetComputePipeline());
		BindEvents(graphics, pipeline, &source, 4);
		list->Dispatch((source.inheritedCapacity + 255) / 256, 1, 1);
		command.UAVBarrierAll();
		source.resetInherited = false;
	}
	if (!source.childCapacity) return;
	list->SetComputeRootSignature(pipeline.GetRootSignature());
	list->SetPipelineState(pipeline.GetComputePipeline());
	BindEvents(graphics, pipeline, &source, 1);
	list->Dispatch(1, 1, 1);
	command.UAVBarrierAll();
	if (source.childCapacity && eventCapacity_) {
		BindEvents(graphics, pipeline, &source, 2);
		list->Dispatch((eventCapacity_ + 255) / 256, 1, 1);
		command.UAVBarrierAll();
	}
	source.children.Transition(command, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	source.childCounts.Transition(command, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
}

void ParticleGPUFeedbackWorld::BindSource(GraphicsCore& graphics, const PipelineState& pipeline, ParticleGPUState& state, bool allowEvents) {

	const auto found = sources_.find(&state);
	BindEvents(graphics, pipeline, found != sources_.end() ? found->second.get() : nullptr, 0, allowEvents);
}

void ParticleGPUFeedbackWorld::ConsumeChildren(ParticleGPUState& state) {

	const auto found = sources_.find(&state);
	if (found != sources_.end()) found->second->childrenConsumed = true;
}

const std::string& ParticleGPUFeedbackWorld::GetDiagnostic(const ParticleGPUState& state) const {

	static const std::string empty;
	const auto found = sources_.find(&state);
	if (found != sources_.end() && !found->second->diagnostic.empty()) return found->second->diagnostic;
	if (!diagnostic_.empty()) return diagnostic_;
	return statisticsDiagnostic_.empty() ? empty : statisticsDiagnostic_;
}

bool ParticleGPUFeedbackWorld::HasRetainedState() const {

	if (decalCapacity_) {
		const uint64_t fence = decalEmissionSubmission_ ? decalEmissionSubmission_->GetFenceValue() : 0;
		const bool emissionPending = decalEmissionSequence_ != 0 &&
			(statisticsSequence_ < decalEmissionSequence_ || fence == 0 || statisticsFence_ < fence);
		if (!statisticsValid_ || emissionPending || statistics_.activeDecals != 0) return true;
	}
	for (const auto& [state, source] : sources_) {
		const auto owner = source->owner.lock();
		if (!source->inheritedCapacity || !owner || owner->rejected) continue;
		if (!owner->statisticsValid || owner->completedSequence != owner->lastSubmittedSequence ||
			!owner->pending.empty() || owner->statistics.particleCount != 0) return true;
	}
	return false;
}

bool ParticleGPUFeedbackWorld::RequiresSimulation(const ParticleGPUState& state) const {

	const auto found = sources_.find(&state);
	if (found == sources_.end()) return false;
	const auto& source = *found->second;
	return source.incoming || !source.routeData.empty() || source.inheritedCapacity != 0;
}

void ParticleGPUFeedbackWorld::ClearSurfaceHistory() {

	receivers_.clear();
	emptyPaint_.reset();
	styleLookup_.clear();
	styleData_.clear();
	styleTextures_.clear();
	for (auto& buffer : events_) buffer.Release();
	for (auto& buffer : eventCounts_) buffer.Release();
	decalStyles_.Release();
	decals_.Release();
	decalFreeList_.Release();
	decalArguments_.Release();
	decalBins_.Release();
	decalBinWords_.Release();
	std::erase_if(sources_, [](const auto& entry) { return entry.second->owner.expired(); });
	for (auto& [state, source] : sources_) {
		source->routes.Release();
		source->children.Release();
		source->childCounts.Release();
		source->routeData.clear();
		source->diagnostic.clear();
		source->childCapacity = 0;
		source->prepared = source->incoming = source->active = false;
		source->preparedFrame = UINT64_MAX;
		source->childrenConsumed = true;
	}
	identityData_.clear();
	statistics_ = {};
	diagnostic_.clear();
	statisticsDiagnostic_.clear();
	statisticsValid_ = false;
	statisticsFence_ = 0;
	surfaceSequence_ = statisticsSequence_ = 0;
	decalEmissionSequence_ = 0;
	decalEmissionSubmission_.reset();
	decalEmissionPossible_ = false;
	initialized_ = false;
	eventsInitialized_ = false;
	defaultsFrameSerial_ = UINT64_MAX;
	resetDecals_ = true;
	eventCapacity_ = receiverCount_ = decalCapacity_ = systemCount_ = 0;
	allocatedDecalCapacity_ = 0;
	for (auto& readback : readbacks_) readback.submission.reset();
}

void ParticleGPUFeedbackWorld::Clear() {

	ClearSurfaceHistory();
	sources_.clear();
	counters_.Release();
	emptyChildren_.Release();
	emptyChildCounts_.Release();
	emptyInheritedStates_.Release();
	systems_.Release();
	emptyRoutes_.Release();
	emptyEvents_.Release();
	emptyCounts_.Release();
	paintReceivers_.Release();
	constants_.Release();
	constantFrameSerial_ = UINT64_MAX;
}
