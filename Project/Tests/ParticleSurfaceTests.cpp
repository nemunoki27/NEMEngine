#include "ParticleSurfaceTests.h"
#include "TestRunner.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleSystemSettings.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleCustomDataModule.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Particle/ParticleGPUMaterialCurves.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/ParticleCameraDepthPreparation.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Buffers/RenderBufferRegistry.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphCompiler.h>
#include <Engine/Core/Rendering/Particle/ParticleGPUState.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Systems/Effect/ParticleDefinitionCompiler.h>

// c++
#include <cmath>
#include <algorithm>

using namespace Engine;

namespace {

	// GPUと同じ線形補間で曲線を取得する
	Vector4 Sample(const ParticleGPUMaterialCurves& curves, uint32_t headerIndex, float progress, bool second = false) {

		const auto header = curves.headers[headerIndex];
		if (header.count == 1) return second ? curves.samples[header.offset].value1 : curves.samples[header.offset].value0;
		const float coordinate = progress * (header.count - 1);
		uint32_t index = (std::min)(uint32_t(coordinate), header.count - 2);
		float blend = coordinate - index;
		if (curves.samples[header.offset].timing.y != 0) {
			const auto begin = curves.samples.begin() + header.offset;
			const auto end = begin + header.count;
			const auto found = std::upper_bound(begin, end, progress, [](float time, const auto& sample) {
				return time < sample.timing.x;
			});
			index = (std::min)(uint32_t(found - begin - (found != begin ? 1 : 0)), header.count - 2);
			const float start = curves.samples[header.offset + index].timing.x;
			blend = std::clamp((progress - start) / (curves.samples[header.offset + index + 1].timing.x - start), 0.0f, 1.0f);
		}
		const auto& a = curves.samples[header.offset + index];
		const auto& b = curves.samples[header.offset + index + 1];
		return second ? a.value1 + (b.value1 - a.value1) * blend : a.value0 + (b.value0 - a.value0) * blend;
	}

	// 別粒子の寿命時刻と編集後の曲線を比較する
	bool CheckCurves() {

		ParticleCustomParameterLayout layout;
		layout.stride = 16;
		ShaderConstantBufferVariable variable;
		variable.name = "Value";
		variable.size = 16;
		variable.valueType = D3D_SVT_FLOAT;
		variable.rows = 1;
		variable.columns = 4;
		layout.variables.push_back(variable);
		ParticleMaterialAnimatedParameter value;
		value.componentCount = 4;
		value.mode = ParticleMaterialParameterMode::OverLifetime;
		value.start = Vector4(1, 2, 3, 4);
		value.end = Vector4(5, 7, 9, 11);
		value.easingType = EasingType::EaseOutSine;
		value.loop.loopCount = 3;
		value.loop.type = ParticleLoopType::PingPong;
		for (const auto loop : {ParticleLoopType::PingPong, ParticleLoopType::Repeat}) for (const bool stepped : {false, true}) {
			value.loop.type = loop;
			value.useCurve = stepped;
			const auto setChannel = [](CurveChannel& channel, float start, float end) {
				channel.keys.clear();
				channel.AddKey(0, start, CurveInterpolationMode::Constant);
				channel.AddKey(0.2134f, end, CurveInterpolationMode::Constant);
				channel.AddKey(0.6123f, start, CurveInterpolationMode::Constant);
				channel.AddKey(1, end, CurveInterpolationMode::Constant);
			};
			for (uint32_t i = 0; i < 3; ++i) setChannel(value.curve3.channels[i], (&value.start.x)[i], (&value.end.x)[i]);
			setChannel(value.curveW.channel, value.start.w, value.end.w);
			ParticlePhaseMaterialSettings phase;
			phase.parameters["Value"] = value;
			ParticleCustomDataSettings custom;
			custom.enabled = true;
			custom.customData1 = value;
			custom.customData2.constant = Vector4(5, 6, 7, 8);
			const auto curves = BuildParticleGPUMaterialCurves(layout, phase, custom);
			const auto check = [&](float progress) {
				const auto expected = EvaluateParticleMaterialParameter(value, progress);
				for (const auto index : {0u, 1u}) {
					const auto difference = Sample(curves, index, progress) - expected;
					if (std::abs(difference.x) > 0.001f || std::abs(difference.w) > 0.001f) return false;
				}
				return Sample(curves, 0, progress, true) == custom.customData2.constant;
			};
			for (uint32_t i = 0; i < 1003; ++i) if (!check(float(i) / 1002)) return false;
			for (uint32_t cycle = 0; cycle < 3; ++cycle) for (const float key : {0.0f, 0.2134f, 0.6123f, 1.0f}) {
				for (const bool reverse : {false, true}) {
					const float time = (cycle + (reverse ? 1 - key : key)) / 3;
					if (!check(time) || !check(std::nextafter(time, 0.0f)) || !check(std::nextafter(time, 1.0f))) return false;
				}
			}
		}
		return ParticleStableRandom(44) == ParticleStableRandom(44) &&
			ParticleStableRandom(44) != ParticleStableRandom(45) && ParticleStableRandom(UINT64_MAX) < 1.0f;
	}

	// 同じWorld内のCamera破棄と世代更新で前回入力を残さない
	bool CheckCameraLifetime() {

		ECSWorld world(ECSWorldKind::Runtime);
		const Entity owner = world.CreateEntity();
		world.AddComponent<ParticleSystemComponent>(owner);
		world.GetComponent<ParticleSystemComponent>(owner).simulationTarget = ParticleSimulationTarget::GPU;
		auto settings = MakeDefaultParticleSystemSettings();
		settings.source.kind = ParticleSourceKind::CameraSurface;
		ParticleDefinitionCompiler compiler;
		auto state = std::make_shared<ParticleGPUState>();
		state->definition = compiler.Resolve(std::make_shared<const ParticleSystemSettings>(settings));
		state->program = std::make_shared<ParticleGPUProgram>();
		TryGetParticleSystemRuntime(world, owner)->instance.emitter.gpu = state;
		const Entity camera = world.CreateEntity();
		world.AddComponent<TransformComponent>(camera);
		world.AddComponent<PerspectiveCameraComponent>(camera);
		GraphicsCore graphics;
		RenderBackendRegistry backends;
		RenderPipelineDeps deps;
		deps.backendRegistry = &backends;
		RenderFrameRequest request;
		request.world = &world;
		RenderBufferRegistry buffers;
		ParticleCameraDepthPreparation preparation;
		auto inputs = preparation.Prepare(graphics, request, nullptr, {}, camera, deps, buffers);
		if (!preparation.NeedsCapture(camera) || inputs.resolveCamera(camera).colorSRV.ptr) return false;
		world.DestroyEntity(camera);
		world.FlushPendingDestroyEntities();
		const Entity replacement = world.CreateEntity();
		world.AddComponent<TransformComponent>(replacement);
		world.AddComponent<PerspectiveCameraComponent>(replacement);
		if (replacement.index != camera.index || replacement.generation == camera.generation) return false;
		inputs = preparation.Prepare(graphics, request, nullptr, {}, camera, deps, buffers);
		return inputs.mainCamera == replacement && !preparation.NeedsCapture(camera) &&
			preparation.NeedsCapture(replacement) && !inputs.resolveDepth(camera).srv.ptr &&
			!inputs.resolveCamera(camera).colorSRV.ptr && !inputs.resolveCamera(replacement).colorSRV.ptr;
	}
}

bool NEMTests::TestParticleSurfaceRendering() {

	ParticleSystemSettings settings = MakeDefaultParticleSystemSettings();
	if (settings.surface.lit || settings.surface.softParticles) return false;
	settings.surface.lit = settings.surface.softParticles = true;
	settings.surface.normalTexture = AssetID{31, 32};
	settings.surface.softDistance = 0.37f;
	ParticleSystemSettings restored;
	if (!FromJson(ToJson(settings), restored) || restored.surface.normalTexture != settings.surface.normalTexture ||
		!restored.surface.lit || !restored.surface.softParticles || restored.surface.softDistance != 0.37f) return false;
	ParticleCustomDataModule module;
	auto custom = module.GetSettings();
	custom.customData1.constant = Vector4(1, 2, 3, 4);
	custom.customData2.constant = Vector4(5, 6, 7, 8);
	module.SetSettings(custom);
	settings.phases[0].modules = {{"CustomData", module.ToJson()}};
	ParticleDefinitionCompiler compiler;
	const auto definition = compiler.Resolve(std::make_shared<const ParticleSystemSettings>(settings));
	const auto program = CompileParticleGPUProgram(*definition);
	if (!program.unsupportedReason.empty()) return false;
	const auto render = MakeParticleRenderSettings(settings.space, settings);
	if (render.phaseCustomData.size() != 1 || !render.phaseCustomData[0].enabled ||
		render.phaseCustomData[0].customData1.constant != custom.customData1.constant) return false;
	for (const auto target : {ShaderGraphTarget::Particle, ShaderGraphTarget::Trail}) {
		auto graph = CreateDefaultSurfaceShaderGraph("ParticleData", target);
		const auto dataID = Engine::UUID::New();
		graph.nodes.push_back(ShaderGraphNode{.id = dataID, .kind = ShaderGraphNodeKind::ParticleData});
		for (uint32_t output = 0; output < 8; ++output) {
			std::erase_if(graph.links, [&](const auto& link) { return link.inputNode == graph.outputNode && link.inputSlot == 0; });
			graph.links.push_back(ShaderGraphLink{.id = Engine::UUID::New(), .outputNode = dataID, .outputSlot = output,
				.inputNode = graph.outputNode, .inputSlot = 0});
			const auto generated = ShaderGraphCompiler::Compile(graph, "surface.hlsli");
			if (!generated.Succeeded() || generated.particleGPUPixelHLSL.empty()) return false;
		}
	}
	return RunTest("ParticleMaterialCurves", CheckCurves) && RunTest("ParticleCameraLifetime", CheckCameraLifetime);
}
