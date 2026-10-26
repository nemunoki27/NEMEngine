#include "ParticleGPUResources.h"
#include "ParticleGPUEnvironment.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Components/Rendering/ParticlePaintReceiverComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDREDDiagnostics.h>

// c++
#include <algorithm>
#include <exception>
#include <stdexcept>

using namespace Engine;

ParticleGPUService::ParticleGPUService() = default;
ParticleGPUService::~ParticleGPUService() {

	if (indexedSignature_) retirement_->Retire(std::move(indexedSignature_));
	if (trailSignature_) retirement_->Retire(std::move(trailSignature_));
}

void ParticleGPUService::UpdateWorld(GraphicsCore& graphics, ECSWorld& world,
	RenderAssetLibrary& assets, PipelineStateCache& pipelines, const ParticleGPUFrameInputs& inputs) {

	if (!environment_ || environment_->lifetime != world.GetLifetime()) {
		environment_ = std::make_unique<ParticleGPUEnvironment>();
		environment_->lifetime = world.GetLifetime();
		instances_.clear();
	}
	auto& environment = *environment_;
	const bool sceneRemoved = std::any_of(environment.sceneInstances.begin(), environment.sceneInstances.end(), [&](const auto& scene) {
		return std::find(inputs.sceneInstances.begin(), inputs.sceneInstances.end(), scene) == inputs.sceneInstances.end();
	});
	if (sceneRemoved) { environment.feedback.ClearSurfaceHistory(); ++feedbackRevision_; }
	environment.sceneInstances = inputs.sceneInstances;
	environment.deltaTime = (std::max)(inputs.deltaTime, 0.0f);
	environment.feedbackUpdate = false;
	environment.mainCamera = inputs.mainCamera;
	bool feedbackActive = environment.feedback.HasRetainedState();
	environment.feedbackUpdate = feedbackActive && inputs.deltaTime > 0;
	bool worldInputsRequested = false;
	world.ForEach<ParticleSystemRuntimeComponent>([&](Entity entity, const ParticleSystemRuntimeComponent&) {
		const auto* runtime = TryGetParticleSystemRuntime(world, entity);
		if (!runtime || !runtime->instance.emitter.gpu) return;
		const auto& state = *runtime->instance.emitter.gpu;
		if (!state.definition || state.rejected) return;
		const auto& feedback = state.definition->settings->feedback;
		worldInputsRequested |= state.program->usesWorldInputs;
		feedbackActive |= !feedback.events.empty() || feedback.paint.enabled || feedback.decal.enabled;
		float elapsed = 0;
		for (const auto& step : state.pending) {
			elapsed += step.deltaTime;
			environment.feedbackUpdate |= step.deltaTime > 0 || step.spawnCount > 0;
		}
		environment.deltaTime = (std::max)(environment.deltaTime, elapsed);
	});
	if (worldInputsRequested)
		environment.inputs.BeginFrame(world, environment.deltaTime, environment.fieldAssets.MakeLoaders(*assets.GetDatabase()));
	const auto getPipeline = [&](AssetID id) {
		return pipelines.GetORCreate(graphics.GetDXObject(), assets, id, PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN);
	};
	const PipelineState* eventPrepare = nullptr;
	if (feedbackActive && environment.feedbackUpdate) {
		eventPrepare = getPipeline(BuiltinAssets::Pipelines::ParticleGPUEventPrepare);
		if (!eventPrepare) throw std::runtime_error("GPUイベントの準備Shaderを生成できません");
		environment.feedback.BeginUpdate(graphics, world, assets, *eventPrepare);
	} else environment.feedbackUpdate = false;
	// WorldやComponentを失った資源もGPU完了までは回収窓口へ残す
	std::erase_if(instances_, [](const auto& entry) { return entry.second->owner.expired(); });
	world.ForEach<ParticleSystemRuntimeComponent>([&](const Entity& entity, const ParticleSystemRuntimeComponent&) {
		auto* runtime = TryGetParticleSystemRuntime(world, entity);
		if (!runtime || !runtime->instance.emitter.gpu) return;
		auto& state = *runtime->instance.emitter.gpu;
		if (state.rejected) {
			instances_.erase(&state);
			return;
		}
		if (!state.definition || !state.program) return;
		// 停止と最後の読戻しを確認してから大きなGPU領域を返す
		if (!runtime->playing && !runtime->paused && state.pending.empty() && state.statisticsValid &&
			state.completedSequence == state.lastSubmittedSequence &&
			state.statistics.particleCount == 0 && state.statistics.trailCount == 0) {
			instances_.erase(&state);
			return;
		}
		try {
			if (graphics.GetDXObject().GetFeatureController().GetSupport().highestShaderModel < D3D_SHADER_MODEL_6_0)
				throw std::runtime_error("GPU粒子にはShader Model 6.0が必要です");
			auto& instance = instances_[&state];
			if (!instance) {
				auto candidate = std::make_unique<ParticleGPUInstance>();
				candidate->Init(graphics, runtime->instance.emitter.gpu);
				instance = std::move(candidate);
			}
			CollectStatistics(graphics, *instance, state);
			// 重いノイズの命令を使わないSystemでは専用Shaderを避ける
			const bool extended = state.program->usesWorldInputs || environment.feedback.RequiresSimulation(state) ||
				state.definition->settings->source.kind != ParticleSourceKind::Analytic;
			const AssetID simulationPipeline = extended ? BuiltinAssets::Pipelines::ParticleGPUExtendedSimulate : state.program->usesCurlNoise ? BuiltinAssets::Pipelines::ParticleGPUCurlSimulate :
				BuiltinAssets::Pipelines::ParticleGPUSimulate;
			const auto* pipeline = pipelines.GetORCreate(graphics.GetDXObject(), assets,
				simulationPipeline, PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN);
			if (!pipeline || !pipeline->GetComputePipeline()) throw std::runtime_error("GPU粒子の更新Shaderを生成できません");
			const PipelineState* trailPipeline = nullptr;
			const PipelineState* ribbonPipeline = nullptr;
			if (state.definition->settings->trail.enabled) {
				trailPipeline = pipelines.GetORCreate(graphics.GetDXObject(), assets, BuiltinAssets::Pipelines::ParticleGPUTrailUpdate,
					PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN);
				if (!trailPipeline) throw std::runtime_error("GPU Trailの更新Shaderを生成できません");
				if (state.definition->settings->trail.mode == ParticleTrailMode::Ribbon) {
					ribbonPipeline = pipelines.GetORCreate(graphics.GetDXObject(), assets, BuiltinAssets::Pipelines::ParticleGPURibbonSelect,
						PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN);
					if (!ribbonPipeline) throw std::runtime_error("GPU Ribbonの選別Shaderを生成できません");
				}
			}
			auto worldInputs = state.program->usesWorldInputs ?
				environment.inputs.Build(world, entity, state.program->splines, state.program->collision, inputs.mainCamera) : environment.emptyInputs;
			if (extended) instance->worldInputs.Upload(worldInputs);
			instance->collisionDepth = worldInputs->depth.requested && inputs.resolveDepth ? inputs.resolveDepth(worldInputs->depth.camera) : ParticleGPUDepthInput{};
			ParticleGPUSimulationStep retained;
			retained.definition = state.definition;
			retained.program = state.program;
			retained.emitterWorld = instance->lastEmitterWorld;
			const auto& sourceStep = state.pending.empty() ? retained : state.pending.back();
			if (extended && !instance->source.Prepare(graphics, world, entity, sourceStep, assets, pipelines, inputs, environment.deltaTime)) {
				state.diagnostic = instance->source.GetDiagnostic();
				// 発生元の準備待ちでも消去と初期化を先に反映する
				if (instance->epoch != state.epoch) {
					auto pending = std::move(state.pending);
					state.pending.clear();
					Simulate(graphics, *instance, *pipeline, trailPipeline, ribbonPipeline, state);
					state.pending = std::move(pending);
					ScheduleReadback(graphics, *instance, state);
				}
				// 読込中の時間は進めず、発生要求だけを上限内で残す
				if (!state.pending.empty()) {
					auto waiting = state.pending.back();
					waiting.spawnCount = 0;
					waiting.birthSequence = state.pending.front().birthSequence;
					for (const auto& queued : state.pending) waiting.spawnCount = (std::min)(instance->capacity, waiting.spawnCount + queued.spawnCount);
					waiting.deltaTime = 0;
					state.pending.assign(1, std::move(waiting));
				}
				return;
			}
			state.diagnostic = worldInputs->diagnostic;
			if (eventPrepare) environment.feedback.PrepareSource(graphics, state, *eventPrepare);
			Simulate(graphics, *instance, *pipeline, trailPipeline, ribbonPipeline, state);
			instance->source.Finish(graphics);
			ScheduleReadback(graphics, *instance, state);
			std::erase_if(instance->views, [](const auto& view) {
				return HasExpiredGraphicsResource(view.second->lastUsedFrame, GraphicsFrameState::GetFrameSerial());
			});
		} catch (const std::exception& error) {
			state.Reject(error.what());
		}
	});
	if (eventPrepare) {
		const auto* paint = getPipeline(BuiltinAssets::Pipelines::ParticleGPUPaint);
		const auto* decal = getPipeline(BuiltinAssets::Pipelines::ParticleGPUImpactDecal);
		if (!paint || !decal) throw std::runtime_error("GPU表面の更新Shaderを生成できません");
		environment.feedback.EndUpdate(graphics, world, assets, *paint, *decal, environment.deltaTime);
		++feedbackRevision_;
	} else {
		bool paintReceivers = false;
		world.ForEach<ParticlePaintReceiverComponent>([&](Entity, const ParticlePaintReceiverComponent&) { paintReceivers = true; });
		if (paintReceivers) {
			const auto* paint = getPipeline(BuiltinAssets::Pipelines::ParticleGPUPaint);
			if (paint) environment.feedback.RefreshPaint(graphics, world, *paint);
		}
	}
	world.ForEach<ParticleSystemRuntimeComponent>([&](Entity entity, const ParticleSystemRuntimeComponent&) {
		auto* runtime = TryGetParticleSystemRuntime(world, entity);
		if (!runtime || !runtime->instance.emitter.gpu) return;
		auto& state = *runtime->instance.emitter.gpu;
		const auto& feedbackDiagnostic = environment.feedback.GetDiagnostic(state);
		if (!state.rejected && !feedbackDiagnostic.empty()) state.diagnostic = feedbackDiagnostic;
	});
}

void ParticleGPUService::PublishFeedback(GraphicsCore& graphics, RenderBufferRegistry& registry) {

	if (!environment_) environment_ = std::make_unique<ParticleGPUEnvironment>();
	environment_->feedback.Publish(graphics, registry);
}

void ParticleGPUService::CollectStatistics(GraphicsCore& graphics, ParticleGPUInstance& instance, ParticleGPUState& state) {

	const uint64_t completed = graphics.GetDXObject().GetCommandQueue()->GetCompletedFenceValue();
	for (auto& readback : instance.readbacks) {
		if (!readback.submission) continue;
		const uint64_t fence = readback.submission->GetFenceValue();
		if (fence == 0 || completed < fence) continue;
		// 再生をやり直した後へ古い結果を適用しない
		if (readback.epoch == state.epoch && readback.sequence >= state.completedSequence) {
			state.statistics = readback.buffer.GetReadbackData();
			state.statisticsValid = true;
			state.completedSequence = readback.sequence;
		}
		readback.submission.reset();
	}
}

void ParticleGPUService::ScheduleReadback(GraphicsCore& graphics, ParticleGPUInstance& instance, const ParticleGPUState& state) {

	if (state.statisticsValid && state.completedSequence == state.lastSubmittedSequence) return;
	auto& commands = *graphics.GetDXObject().GetDxCommand();
	for (auto& readback : instance.readbacks) {
		if (readback.submission) continue;
		instance.statistics.Transition(commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
		commands.GetCommandList()->CopyBufferRegion(readback.buffer.GetResource(), 0,
			instance.statistics.GetResource(), 0, sizeof(ParticleStatisticsGPU));
		instance.statistics.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		readback.submission = commands.GetSubmissionTicket();
		readback.epoch = state.epoch;
		readback.sequence = state.lastSubmittedSequence;
		return;
	}
	// 空きがなければ次frameへ送り、結果取得のために待たない
}

void ParticleGPUService::DrawIndexed(GraphicsCore& graphics, const ParticleGPUDrawData& data, uint32_t phaseIndex) {

	graphics.GetDXObject().GetDxCommand()->GetCommandList()->ExecuteIndirect(GetDrawSignature(graphics, true), 1, data.arguments,
		uint64_t(phaseIndex) * sizeof(D3D12_DRAW_INDEXED_ARGUMENTS), nullptr, 0);
}

void ParticleGPUService::DrawTrails(GraphicsCore& graphics, const ParticleGPUDrawData& data) {

	graphics.GetDXObject().GetDxCommand()->GetCommandList()->ExecuteIndirect(GetDrawSignature(graphics, false), 1,
		data.trailArguments, 0, nullptr, 0);
}

ID3D12CommandSignature* ParticleGPUService::GetDrawSignature(GraphicsCore& graphics, bool indexed) {

	auto& platform = graphics.GetDXObject();
	auto& signature = indexed ? indexedSignature_ : trailSignature_;
	if (!signature) {
		D3D12_INDIRECT_ARGUMENT_DESC argument{};
		argument.Type = indexed ? D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED : D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
		D3D12_COMMAND_SIGNATURE_DESC descriptor{};
		descriptor.ByteStride = indexed ? sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) : sizeof(D3D12_DRAW_ARGUMENTS);
		descriptor.NumArgumentDescs = 1;
		descriptor.pArgumentDescs = &argument;
		ComPtr<ID3D12CommandSignature> candidate;
		const HRESULT result = platform.GetDevice()->CreateCommandSignature(&descriptor, nullptr, IID_PPV_ARGS(&candidate));
		if (!DxDREDDiagnostics::CheckHRESULT(platform.GetDevice(), result, "ParticleGPU::CreateCommandSignature"))
			throw std::runtime_error("GPU粒子の描画命令を作成できません");
		retirement_ = &platform.GetResourceRetirement();
		signature = std::move(candidate);
	}
	return signature.Get();
}
