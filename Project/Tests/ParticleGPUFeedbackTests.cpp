#include "ParticleFeedbackTests.h"
#include "ParticleGPUFixture.h"
#include "ParticleGPUFeedbackFixture.h"
#include "ParticleGPUExtendedIntegrationTests.h"
#include "ParticleImpactDecalRasterTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleGPUFeedbackTypes.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTexture2D.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxRenderTargetView.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>

// c++
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>

using namespace Engine;
using namespace NEMTests;

namespace {

	using FeedbackFixture = ParticleGPUFeedbackFixture;

	// Clamp後の同一点でも判定を固定せず同じ列を再現する
	bool CheckProbability(FeedbackFixture& input, const PipelineState& pipeline, const PipelineState& prepare) {

		input.values.reserved = 1;
		input.values.routeCount = 1;
		input.route.flags = 0;
		input.route.count = 1;
		input.route.probability = 0.5f;
		uint32_t previous = 0;
		for (uint32_t run = 0; run < 2; ++run) {
			input.Dispatch(prepare, 0);
			input.Dispatch(pipeline, 0);
			const auto bytes = input.fixture.Read(input.counts, sizeof(uint32_t));
			uint32_t count;
			std::memcpy(&count, bytes.data(), sizeof(count));
			if (count < 64 || count > 192 || (run != 0 && count != previous)) return false;
			previous = count;
		}
		return true;
	}

	// Color更新の有無と通常粒子へのSlot再利用を確認する
	bool CheckInheritedState(FeedbackFixture& input, const PipelineState& pipeline) {

		input.values.reserved = 1;
		input.previousData.assign(1, ParticleGPUEvent{});
		input.previousData[0].color = Vector4(0.25f, 0.5f, 0.75f, 0.5f);
		input.previousData[0].seed = 832;
		input.previousData[0].flags = 24;
		for (uint32_t mode = 0; mode < 3; ++mode) {
			input.writesColor = mode == 1;
			input.Dispatch(pipeline, mode);
			input.events.Transition(input.fixture.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
			const auto bytes = input.fixture.ReadResource(input.events.GetResource(), sizeof(ParticleGPUEvent));
			ParticleGPUEvent sample;
			std::memcpy(&sample, bytes.data(), sizeof(sample));
			if (mode < 2 && (sample.color != input.previousData[0].color || sample.seed != 832)) return false;
			if (mode == 2 && (sample.color != Vector4(1, 1, 1, 1) || sample.seed != 77)) return false;
		}
		input.values.reserved = 0;
		input.writesColor = false;
		return true;
	}

	// 検証する1画素だけをGPU完了後に読む
	Vector4 ReadPaintPixel(ParticleGPUFixture& fixture, RenderTexture2D& texture) {

		texture.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
		ComPtr<ID3D12Resource> readback;
		DxUtils::CreateReadbackBufferResource(fixture.device.Get(), readback, 256);
		D3D12_TEXTURE_COPY_LOCATION destination{};
		destination.pResource = readback.Get();
		destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		destination.PlacedFootprint.Footprint = {DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 1, 1, 256};
		D3D12_TEXTURE_COPY_LOCATION source{};
		source.pResource = texture.GetResource();
		source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		const D3D12_BOX box{16, 16, 0, 17, 17, 1};
		fixture.commands.GetCommandList()->CopyTextureRegion(&destination, 0, 0, 0, &source, &box);
		fixture.Finish();
		void* mapped = nullptr;
		const D3D12_RANGE range{0, sizeof(Vector4)};
		if (FAILED(readback->Map(0, &range, &mapped))) return Vector4(-1, -1, -1, -1);
		Vector4 result;
		std::memcpy(&result, mapped, sizeof(result));
		const D3D12_RANGE empty{0, 0};
		readback->Unmap(0, &empty);
		return result;
	}

	// UVと平面投影を同じ実行時Textureへ蓄積して明示消去する
	bool CheckPaint(FeedbackFixture& input, const PipelineState& emit, const PipelineState& prepare, const PipelineState& pipeline) {

		auto& fixture = input.fixture;
		RTVDescriptor targets;
		targets.Init(fixture.device.Get(), {D3D12_DESCRIPTOR_HEAP_TYPE_RTV});
		targets.SetRetirementQueue(fixture.retirement);
		RenderTexture2D texture;
		texture.Create(fixture.device.Get(), &targets, &fixture.descriptors,
			{.width = 32, .height = 32, .format = DXGI_FORMAT_R32G32B32A32_FLOAT, .createUAV = true});
		StructuredInstanceBuffer<ParticlePaintReceiverGPU> receivers;
		StructuredRWBuffer<uint32_t> tileCounts, tileEvents;
		receivers.Init(fixture.device.Get(), &fixture.descriptors);
		tileCounts.Init(fixture.device.Get(), &fixture.descriptors); tileCounts.EnsureCapacity(4);
		tileEvents.Init(fixture.device.Get(), &fixture.descriptors); tileEvents.EnsureCapacity(8);
		struct Constants { ParticlePaintReceiverGPU receiver; uint32_t resolution, tiles, operation, tileCapacity; };
		Constants constants;
		constants.receiver.entity = Entity{3, 4};
		constants.receiver.scaleOpacity = Vector4(0.5f, 0.5f, 1, 0);
		constants.receiver.projection = 1;
		constants.resolution = 32;
		constants.tiles = constants.tileCapacity = 2;
		input.values.source = ParticleGPUIdentity{Entity{1, 0}, 11, 1};
		input.route.flags = 64;
		input.route.count = 1;
		input.route.parameters = Vector4(0.5f, 0.8f, 0, 0);
		input.Dispatch(prepare, 3); input.Dispatch(prepare, 0); input.Dispatch(emit, 0);
		const auto dispatch = [&](uint32_t operation) {
			input.Bind(pipeline, 0);
			texture.Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			tileCounts.Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			tileEvents.Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			const std::array<ParticlePaintReceiverGPU, 1> data{constants.receiver};
			receivers.Upload(data);
			auto* commands = fixture.commands.GetCommandList();
			if (const auto* binding = pipeline.FindBindingByName("gParticlePaintReceivers", ShaderBindingKind::SRV)) {
				RootBindingCommand::SetComputeSRV(commands, binding, receivers.GetGPUAddress(), receivers.GetGPUHandle());
			}
			if (const auto* binding = pipeline.FindBindingByName("gParticlePaintOutput", ShaderBindingKind::UAV)) {
				RootBindingCommand::SetComputeUAV(commands, binding, 0, texture.GetUAVGPUHandle());
			}
			if (const auto* binding = pipeline.FindBindingByName("gParticlePaintTileCounts", ShaderBindingKind::UAV)) {
				RootBindingCommand::SetComputeUAV(commands, binding, tileCounts.GetGPUAddress(), tileCounts.GetUAVGPUHandle());
			}
			if (const auto* binding = pipeline.FindBindingByName("gParticlePaintTileEvents", ShaderBindingKind::UAV)) {
				RootBindingCommand::SetComputeUAV(commands, binding, tileEvents.GetGPUAddress(), tileEvents.GetUAVGPUHandle());
			}
			constants.operation = operation;
			const auto allocation = fixture.constants.AllocateAndUpload(fixture.retirement, fixture.device.Get(), constants);
			if (const auto* binding = pipeline.FindBindingByName("ParticleGPUPaintConstants", ShaderBindingKind::CBV)) {
				RootBindingCommand::SetComputeCBV(commands, binding, allocation.gpuAddress);
			}
			const uint32_t counts[]{1, 0, 0, 0};
			const auto scene = fixture.constants.AllocateAndUpload(fixture.retirement, fixture.device.Get(), counts);
			if (const auto* binding = pipeline.FindBindingByName("ParticlePaintSceneConstants", ShaderBindingKind::CBV)) {
				RootBindingCommand::SetComputeCBV(commands, binding, scene.gpuAddress);
			}
			commands->Dispatch(operation == 2 || operation == 3 ? 4 : 1, 1, 1);
			fixture.commands.UAVBarrierAll();
		};
		bool valid = true;
		for (uint32_t projection : {1u, 0u}) {
			constants.receiver.projection = projection;
			dispatch(3); dispatch(0); dispatch(1); dispatch(2);
			valid &= ReadPaintPixel(fixture, texture) == Vector4(0.25f, 0.5f, 0.75f, 1);
		}
		dispatch(3);
		valid &= ReadPaintPixel(fixture, texture) == Vector4(0, 0, 0, 0);
		ParticleFeedbackStatisticsGPU statistics;
		const auto bytes = fixture.Read(input.statistics, sizeof(statistics));
		std::memcpy(&statistics, bytes.data(), sizeof(statistics));
		valid &= statistics.paintOverflow > 0;
		// ローカルDescriptorの破棄前に遅延回収を終える
		texture.Destroy();
		fixture.Finish();
		return valid;
	}

	// 古い再生と別Componentからのイベントを受け取らない
	bool CheckEventQueue(FeedbackFixture& input, const PipelineState& emit, const PipelineState& prepare) {

		auto& fixture = input.fixture;
		input.Dispatch(prepare, 3);
		input.Dispatch(prepare, 0);
		input.Dispatch(emit, 0);
		uint32_t count = 0;
		auto bytes = fixture.Read(input.counts, sizeof(count));
		std::memcpy(&count, bytes.data(), sizeof(count));
		if (count != 8) return false;
		input.events.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
		bytes = fixture.ReadResource(input.events.GetResource(), 4 * sizeof(ParticleGPUEvent));
		input.previousData.resize(4);
		std::memcpy(input.previousData.data(), bytes.data(), bytes.size());
		input.previousCount = 4;
		input.values.source = input.identities[1];
		input.Dispatch(prepare, 1);
		input.Dispatch(prepare, 2);
		bytes = fixture.Read(input.childCounts, sizeof(count));
		std::memcpy(&count, bytes.data(), sizeof(count));
		if (count != 12) return false;
		ParticleFeedbackStatisticsGPU statistics;
		bytes = fixture.Read(input.statistics, sizeof(statistics));
		std::memcpy(&statistics, bytes.data(), sizeof(statistics));
		if (statistics.emitted != 4 || statistics.eventOverflow != 4 || statistics.childOverflow != 7) return false;
		for (uint32_t stale = 0; stale < 4; ++stale) {
			input.identities[0] = ParticleGPUIdentity{Entity{1, 0}, 11, 1};
			input.values.source = input.identities[1];
			if (stale == 0) ++input.identities[0].epoch;
			if (stale == 1) ++input.identities[0].token;
			if (stale == 2) ++input.identities[0].entity.generation;
			if (stale == 3) ++input.values.source.epoch;
			input.Dispatch(prepare, 1);
			input.Dispatch(prepare, 2);
			bytes = fixture.Read(input.childCounts, sizeof(count));
			std::memcpy(&count, bytes.data(), sizeof(count));
			if (count != 0) return false;
		}
		return true;
	}

	// 発生元を読まずPool内で寿命と再利用を処理する
	bool CheckImpactPool(FeedbackFixture& input, const PipelineState& emit, const PipelineState& prepare,
		const PipelineState& pipeline) {

		auto& fixture = input.fixture;
		StructuredInstanceBuffer<DecalGPUData> styles;
		StructuredRWBuffer<ParticleImpactDecalGPU> decals;
		StructuredRWBuffer<uint32_t> freeList, bins, binWords;
		StructuredRWBuffer<std::array<uint32_t, 4>> arguments;
		styles.Init(fixture.device.Get(), &fixture.descriptors);
		decals.Init(fixture.device.Get(), &fixture.descriptors); decals.EnsureCapacity(2);
		freeList.Init(fixture.device.Get(), &fixture.descriptors); freeList.EnsureCapacity(3);
		bins.Init(fixture.device.Get(), &fixture.descriptors); bins.EnsureCapacity(257);
		binWords.Init(fixture.device.Get(), &fixture.descriptors); binWords.EnsureCapacity(257);
		arguments.Init(fixture.device.Get(), &fixture.descriptors); arguments.EnsureCapacity(2);
		DecalGPUData style;
		style.tangentX.w = style.tangentY.w = style.direction.w = 1;
		input.values.source = ParticleGPUIdentity{Entity{1, 0}, 11, 1};
		input.route.flags = 128;
		input.route.count = 1;
		input.route.parameters = Vector4(0, 0.5f, 1, 0);
		input.Dispatch(prepare, 3);
		input.Dispatch(prepare, 0);
		input.Dispatch(emit, 0);
		const auto dispatch = [&](uint32_t operation, float deltaTime) {
			input.Bind(pipeline, 0);
			const std::array<DecalGPUData, 1> data{style};
			styles.Upload(data);
			decals.Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			freeList.Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			arguments.Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			bins.Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			binWords.Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			auto* commands = fixture.commands.GetCommandList();
			if (const auto* binding = pipeline.FindBindingByName("gGPUImpactDecalStyles", ShaderBindingKind::SRV)) {
				RootBindingCommand::SetComputeSRV(commands, binding, styles.GetGPUAddress(), styles.GetGPUHandle());
			}
			const auto bind = [&](const char* name, auto& buffer) {
				if (const auto* binding = pipeline.FindBindingByName(name, ShaderBindingKind::UAV)) {
					RootBindingCommand::SetComputeUAV(commands, binding, buffer.GetGPUAddress(), buffer.GetUAVGPUHandle());
				}
			};
			bind("gGPUImpactDecalsOut", decals); bind("gGPUImpactDecalFreeList", freeList); bind("gGPUImpactDecalArguments", arguments);
			bind("gGPUImpactDecalBinsOut", bins); bind("gGPUImpactDecalBinWordsOut", binWords);
			struct Constants { uint32_t capacity, styles, operation; float deltaTime; uint32_t previousCapacity, pad[3]; };
			const Constants values{2, 1, operation, deltaTime, 0, {1, 1, 0}};
			const auto allocation = fixture.constants.AllocateAndUpload(fixture.retirement, fixture.device.Get(), values);
			if (const auto* binding = pipeline.FindBindingByName("ParticleGPUImpactDecalConstants", ShaderBindingKind::CBV)) {
				RootBindingCommand::SetComputeCBV(commands, binding, allocation.gpuAddress);
			}
			commands->Dispatch(operation == 7 ? 2u : 1u, 1, 1);
			fixture.commands.UAVBarrierAll();
		};
		dispatch(6, 0); dispatch(5, 0); dispatch(2, 0); dispatch(3, 0); dispatch(4, 0);
		ParticleFeedbackStatisticsGPU statistics;
		auto bytes = fixture.Read(input.statistics, sizeof(statistics));
		std::memcpy(&statistics, bytes.data(), sizeof(statistics));
		if (statistics.activeDecals != 2 || statistics.decalOverflow != 2) return false;
		dispatch(7, 0); dispatch(8, 0);
		bytes = fixture.Read(bins, sizeof(uint32_t));
		uint32_t candidates;
		std::memcpy(&candidates, bytes.data(), sizeof(candidates));
		if (candidates != 3u) return false;
		dispatch(1, 0.75f);
		decals.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
		bytes = fixture.ReadResource(decals.GetResource(), sizeof(ParticleImpactDecalGPU));
		ParticleImpactDecalGPU sample;
		std::memcpy(&sample, bytes.data(), sizeof(sample));
		if (!sample.alive || std::abs(sample.decal.baseColor.a - 0.5f) > 1e-6f) return false;
		dispatch(1, 0.5f); dispatch(3, 0); dispatch(4, 0);
		bytes = fixture.Read(input.statistics, sizeof(statistics));
		std::memcpy(&statistics, bytes.data(), sizeof(statistics));
		if (statistics.activeDecals != 0) return false;
		dispatch(7, 0); dispatch(8, 0);
		bytes = fixture.Read(bins, sizeof(uint32_t));
		std::memcpy(&candidates, bytes.data(), sizeof(candidates));
		if (candidates != 0u) return false;
		dispatch(2, 0); dispatch(3, 0); dispatch(4, 0);
		bytes = fixture.Read(input.statistics, sizeof(statistics));
		std::memcpy(&statistics, bytes.data(), sizeof(statistics));
		if (statistics.activeDecals != 2) return false;
		arguments.Transition(fixture.commands, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		return CheckParticleImpactDecalRaster(fixture, arguments.GetResource());
	}
}

bool NEMTests::CheckParticleFeedback(ParticleGPUFixture& fixture) {

	if (!CheckParticleGPUExtendedIntegration(fixture)) return false;
	TestDirectory directory("ParticleGPUEventShader");
	const auto path = directory.GetPath() / "eventTest.CS.hlsl";
	std::ofstream stream(path);
	stream << "#include \"" << Algorithm::PathToUTF8(RuntimePaths::GetEngineAssetPath("Rendering/Builtin/Particle/GPU/particleGPUShared.hlsli")) << "\"\n";
	stream << "#define NEM_PARTICLE_GPU_MODULES\nStructuredBuffer<ParticleGPUPhase> gGPUPhases : register(t2);\n";
	stream << "StructuredBuffer<ParticleGPUInstruction> gGPUInstructions : register(t0);\n";
	stream << "#include \"" << Algorithm::PathToUTF8(RuntimePaths::GetEngineAssetPath("Rendering/Builtin/Particle/GPU/particleGPUEvents.hlsli")) << "\"\n";
	stream << R"(
[numthreads(8,1,1)]
void main(uint index : SV_DispatchThreadID) {
    ParticleAttributesGPU particle = (ParticleAttributesGPU)0;
    particle.birthLow = index; particle.color = float4(0.25f,0.5f,0.75f,1);
    ParticleGPUImpact impact = (ParticleGPUImpact)0;
    impact.normal = float3(0,1,0); impact.velocity = float3(1,2,3);
    impact.receiver = uint2(3,4); impact.uv = 0.5f.xx; impact.hasUV = 1;
    ParticleGPUEmitEvent(0,index,particle,impact,0);
}
[numthreads(1,1,1)]
void inheritMain(uint index : SV_DispatchThreadID) {
    ParticleGPUEvent child = gGPUPreviousEvents[0];
    ParticleGPUPrepareInheritedState(0,eventOperation != 2,child);
    ParticleAttributesGPU particle = (ParticleAttributesGPU)0;
    particle.color = 1;
    ParticleGPUApplyInheritedColor(particle,0);
    if(eventOperation == 1) particle.color = 1;
    ParticleGPUApplyInheritedColor(particle,0);
    ParticleGPUEvent output = (ParticleGPUEvent)0;
    output.color = particle.color;
    output.seed = ParticleGPUSeed(0,77);
    gGPUEvents[0] = output;
}
[numthreads(1,1,1)]
void probabilityMain(uint index : SV_DispatchThreadID) {
    ParticleGPUEvent child = (ParticleGPUEvent)0;
    ParticleGPUPrepareInheritedState(0,false,child);
    ParticleAttributesGPU particle = (ParticleAttributesGPU)0;
    particle.birthLow = 183; particle.color = 1; particle.positionAge.w = 1;
    ParticleGPUImpact impact = (ParticleGPUImpact)0;
    impact.normal = float3(0,1,0);
    for(uint eventIndex=0;eventIndex<256;++eventIndex) ParticleGPUEmitEvent(0,0,particle,impact,0);
})";
	stream.close();
	const auto shaderPath = Algorithm::PathToUTF8(std::filesystem::absolute(path));
	auto emit = fixture.MakePipeline(shaderPath.c_str());
	auto prepare = fixture.MakePipeline("particleGPUEventPrepare.CS.hlsl");
	auto decals = fixture.MakePipeline("particleGPUImpactDecal.CS.hlsl");
	auto paint = fixture.MakePipeline("particleGPUPaint.CS.hlsl");
	auto extended = fixture.MakePipeline("particleGPUExtendedSimulate.CS.hlsl");
	ComputePipelineDesc descriptor;
	descriptor.compute = {.file = shaderPath, .entry = "inheritMain", .profile = "cs_6_0"};
	auto inherit = PipelineStateBuilder::CreateCompute(fixture.retirement, fixture.device.Get(), &fixture.compiler, descriptor);
	descriptor.compute.entry = "probabilityMain";
	auto probability = PipelineStateBuilder::CreateCompute(fixture.retirement, fixture.device.Get(), &fixture.compiler, descriptor);
	if (!emit || !prepare || !decals || !paint || !inherit || !extended || !probability) return false;
	for (uint32_t contexts : {1u, 2u}) {
		fixture.ChangeFrameCount(contexts);
		FeedbackFixture input(fixture);
		if (!CheckEventQueue(input, *emit, *prepare) || !CheckImpactPool(input, *emit, *prepare, *decals) ||
			!CheckPaint(input, *emit, *prepare, *paint) || !CheckInheritedState(input, *inherit) ||
			!CheckProbability(input, *probability, *prepare)) return false;
	}
	return fixture.CheckMessages();
}
