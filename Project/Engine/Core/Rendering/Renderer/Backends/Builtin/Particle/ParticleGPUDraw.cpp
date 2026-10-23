#include "ParticleRenderBackend.h"

//============================================================================
//	include
//============================================================================
#include "ParticleDrawPreparation.h"
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/Rendering/Particle/ParticleGPUState.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>

// c++
#include <algorithm>
#include <cmath>

using namespace Engine;

namespace {

	void FillFrustum(const Matrix4x4& matrix, ParticleGPUViewConstants& constants) {

		// 行ベクトルのViewProjectionから平面を取り出す
		const auto column = [&](uint32_t index) {
			return Vector4(matrix.m[0][index], matrix.m[1][index], matrix.m[2][index], matrix.m[3][index]);
		};
		const auto x = column(0);
		const auto y = column(1);
		const auto z = column(2);
		const auto w = column(3);
		const Vector4 planes[] = {w + x, w - x, w + y, w - y, z, w - z};
		for (uint32_t i = 0; i < 6; ++i) {
			const auto& plane = planes[i];
			const float length = std::sqrt(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
			constants.frustum[i] = length > 0 ? plane * (1.0f / length) : Vector4(0, 0, 0, 1);
		}
	}

}

void ParticleRenderBackend::PrepareSimulation(GraphicsCore& graphics, ECSWorld& world,
	RenderAssetLibrary& assets, PipelineStateCache& pipelines) {

	gpuService_.UpdateWorld(graphics, world, assets, pipelines);
}

void ParticleRenderBackend::DrawGPU(const RenderDrawContext& context, const RenderItem& item,
	const ParticleEmitterRuntime& emitter) {

	auto& state = *emitter.gpu;
	if (state.rejected) return;
	const auto& settings = emitter.renderSettings;
	const auto* camera = context.view->FindCamera(item.cameraDomain);
	if (!camera || !camera->valid) return;
	auto& graphics = *context.graphicsCore;
	// 共有Meshか板の描画範囲を用意する
	D3D12_GPU_VIRTUAL_ADDRESS vertices = 0;
	D3D12_INDEX_BUFFER_VIEW indices{};
	uint32_t indexCount = 0;
	float radius = 1;
	const bool drawSource = !settings.trail.enabled || settings.trail.drawSource;
	if (drawSource && settings.model) {
		const auto* mesh = meshResourceManager_.Find(settings.model);
		if (!mesh) {
			meshResourceManager_.RequestMesh(*context.assetDatabase, settings.model);
			meshResourceManager_.FlushUploads();
			mesh = meshResourceManager_.Find(settings.model);
		}
		if (!mesh || !mesh->vertexSRV.buffer) return;
		if (mesh->isSkinned) {
			state.Reject("GPU粒子のMeshはスキニングに対応していません");
			return;
		}
		vertices = mesh->vertexSRV.buffer->GetResource()->GetGPUVirtualAddress();
		indices = mesh->indexBuffer.GetIndexBufferView();
		indexCount = mesh->indexCount;
		radius = mesh->boundsRadius + Vector3::Length(mesh->boundsCenter);
	} else if (drawSource) {
		const auto shape = ParticleDrawPreparation::MakeShapeComponent(settings);
		const auto* geometry = geometryManager_.GetOrCreate(graphics, shape);
		if (!geometry || !geometry->vertexBuffer.buffer) return;
		vertices = geometry->vertexBuffer.buffer->GetResource()->GetGPUVirtualAddress();
		indices = geometry->indexBuffer.GetIndexBufferView();
		indexCount = geometry->indexCount;
		radius = geometry->boundsRadius + Vector3::Length(geometry->boundsCenter);
	}
	// 描画中のCameraからカリングとビルボードの条件を作る
	ParticleGPUViewConstants constants;
	constants.viewProjection = camera->matrices.viewProjectionMatrix;
	constants.cameraPosition = Vector4(camera->cameraPos.x, camera->cameraPos.y, camera->cameraPos.z, 0);
	constants.cameraForward = Vector4(camera->forward.x, camera->forward.y, camera->forward.z,
		camera->projectionMode == ResolvedProjectionMode::Orthographic ? 1.0f : 0.0f);
	constants.indexCount = indexCount;
	constants.phaseCount = static_cast<uint32_t>(state.definition->phases.size());
	constants.blendMode = static_cast<uint32_t>(item.blendMode);
	constants.is2D = settings.space == PrimitiveRenderSpace::Screen2D ? 1u : 0u;
	constants.flipV = constants.is2D && !settings.model && settings.shape == PrimitiveType::Plane ? 1u : 0u;
	constants.geometryRadius = radius;
	constants.sortEnabled = item.blendMode != BlendMode::Add ? 1u : 0u;
	for (const auto axis : settings.billboardAxes) constants.billboardMask |= 1u << static_cast<uint32_t>(axis);
	const auto* cullingCamera = context.cullingView ? context.cullingView->FindCamera(item.cameraDomain) : camera;
	FillFrustum(cullingCamera && cullingCamera->valid ? cullingCamera->matrices.viewProjectionMatrix : constants.viewProjection, constants);
	const auto data = gpuService_.PrepareView(graphics, emitter.gpu, *context.assetLibrary,
		*context.pipelineCache, context.view->GetHistoryKey() + std::to_string(static_cast<uint32_t>(item.cameraDomain)), constants);
	if (!data.arguments) return;
	const auto pipelineID = constants.is2D ? BuiltinAssets::Pipelines::ParticleGPUDraw2D : BuiltinAssets::Pipelines::ParticleGPUDraw;
	const auto* pipeline = context.pipelineCache->GetORCreate(graphics.GetDXObject(), *context.assetLibrary,
		pipelineID, PipelineVariantKind::GraphicsVertex, context.GetRTVFormats(), context.dsvFormat, context.runtimeFeatures);
	if (!pipeline) return;
	if (drawSource) {
		// Phaseごとのマテリアルで可視粒子だけを間接描画する
		for (uint32_t phase = 0; phase < constants.phaseCount; ++phase) {
			const AssetID materialID = phase < settings.phaseMaterials.size() && settings.phaseMaterials[phase] ?
				settings.phaseMaterials[phase] : settings.material;
			BackendDrawCommon::ResolvedMaterialPass material;
			if (!ParticleDrawPreparation::ResolveGPUParticlePass(context, materialID, constants.is2D != 0, material)) {
				state.Reject("GPU粒子は標準Particle Shaderのマテリアルに対応しています");
				return;
			}
			constants.phaseIndex = phase;
			const auto allocation = constantBufferAllocator_.AllocateAndUpload(graphics.GetDXObject().GetResourceRetirement(),
				graphics.GetDXObject().GetDevice(), constants);
			auto* commands = BackendDrawCommon::SetupGraphicsPipeline(context, *pipeline, item.blendMode);
			SyncAndBindRegistry(*pipeline, context, commands);
			RootBindingCommand::SetGraphicsCBV(commands,
				pipeline->FindBindingByName("ParticleGPUViewConstants", ShaderBindingKind::CBV), allocation.gpuAddress);
			const auto srv = [&](const char* name, D3D12_GPU_VIRTUAL_ADDRESS address) {
				RootBindingCommand::SetGraphicsSRV(commands, pipeline->FindBindingByName(name, ShaderBindingKind::SRV), address);
			};
			srv("gVertices", vertices);
			srv("gGPUParticles", data.particles);
			srv("gGPUParents", data.parents);
			srv("gGPUViewKeys", data.indices);
			srv("gGPUViewCounts", data.counts);
			MaterialParameterSet overrides;
			ParticleDrawPreparation::BuildPhaseMaterialInstance(ParticleDrawPreparation::GetPhaseMaterialSettings(settings, phase), overrides);
			BindMaterial(context, *pipeline, *material.material, overrides.empty() ? nullptr : &overrides, commands);
			commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			commands->IASetIndexBuffer(&indices);
			gpuService_.DrawIndexed(graphics, data, phase);
		}
	}
	if (settings.trail.enabled && data.trailArguments) DrawGPUTrails(context, item, emitter, data, constants);
}
