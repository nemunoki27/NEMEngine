#include "HeightFogPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateBuilder.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Lighting/Interface/ILightExtractor.h>
#include <Engine/Core/Rendering/Renderer/Lighting/AtmosphereEnvironment.h>
#include <Engine/Core/World/Components/Lighting/ExponentialHeightFogComponent.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>

// c++
#include <algorithm>

//============================================================================
//	HeightFogPass classMethods
//============================================================================
Engine::HeightFogPass::HeightFogPass() {

	constantSlot_ = bindings_.AddSlotByRegister(ShaderBindingKind::CBV, 10, 7);
	depthSlot_ = bindings_.AddSlotByRegister(ShaderBindingKind::SRV, 0, 0);
}

bool Engine::HeightFogPass::EnsurePipeline(GraphicsCore& graphics, DXGI_FORMAT format) {

	if (pipeline_ && format_ == format) return true;
	GraphicsPipelineDesc desc{};
	desc.type = PipelineType::Vertex;
	desc.preRaster.shader = BuiltinAssets::Shaders::Skybox;
	desc.preRaster.file = "Builtin/Skybox/skybox.VS.hlsl";
	desc.preRaster.entry = "main";
	desc.preRaster.profile = "vs_6_0";
	desc.pixel.file = "Builtin/Atmosphere/heightFog.PS.hlsl";
	desc.pixel.shader = BuiltinAssets::Shaders::HeightFog;
	desc.pixel.entry = "main";
	desc.pixel.profile = "ps_6_0";
	desc.rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	desc.rasterizer.CullMode = D3D12_CULL_MODE_NONE;
	desc.depthStencil = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	desc.depthStencil.DepthEnable = FALSE;
	desc.depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	desc.depthStencil.StencilEnable = FALSE;
	desc.sampleDesc = {1, 0};
	desc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.numRenderTargets = 1;
	desc.rtvFormats[0] = format;
	desc.dsvFormat = DXGI_FORMAT_UNKNOWN;
	D3D12_STATIC_SAMPLER_DESC sampler{};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderRegister = 10;
	sampler.RegisterSpace = 7;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	desc.staticSamplers.push_back(sampler);
	pipeline_ = PipelineStateBuilder::CreateGraphics(graphics.GetDXObject().GetResourceRetirement(),
		graphics.GetDXObject().GetDevice(), graphics.GetDXObject().GetDxShaderCompiler(), desc);
	format_ = format;
	return pipeline_ != nullptr;
}

void Engine::HeightFogPass::Execute(GraphicsCore& graphics,
	[[maybe_unused]] const RenderPassPhaseBuckets& buckets, SceneExecutionContext& context) {

	HeightFogConstants data{};
	AtmosphereViewData atmosphere{};
	const auto* camera = context.view ? context.view->FindCamera(RenderCameraDomain::Perspective) : nullptr;
	const ExponentialHeightFogComponent* fog = nullptr;
	float height = 0.0f;
	if (camera && camera->valid && context.world) {
		const uint32_t mask = context.view->GetCullingMask(RenderCameraDomain::Perspective);
		data.inverseViewProjection = camera->matrices.inverseProjectionMatrix * camera->matrices.inverseViewMatrix;
		data.cameraPosition = Vector4(camera->cameraPos.x, camera->cameraPos.y, camera->cameraPos.z, 0.0f);
		atmosphere = graphics.GetAtmosphereEnvironment().PrepareView(graphics, *context.world, mask,
			context.view->GetHistoryKey(), camera->cameraPos, data.inverseViewProjection);
		data.aerialIndex = atmosphere.aerialIndex;
		data.aerialMaxDistance = atmosphere.maxDistance;
		data.aerialStrength = atmosphere.strength;
		context.world->ForEach<ExponentialHeightFogComponent>([&](const Entity& entity, const auto& component) {
			if (fog || !RenderItemExtract::IsVisible(*context.world, entity, component.enabled) ||
				(RenderItemExtract::GetVisibilityLayerMask(*context.world, entity, component.renderingLayerMask) & mask) == 0) return;
			fog = &component;
			height = RenderItemExtract::GetWorldMatrix(*context.world, entity).GetTranslationValue().y;
		});
	}
	if (fog) {
		data.inverseViewProjection = camera->matrices.inverseProjectionMatrix * camera->matrices.inverseViewMatrix;
		data.cameraPosition = Vector4(camera->cameraPos.x, camera->cameraPos.y, camera->cameraPos.z, 1.0f);
		data.colorDensity = Vector4(fog->color.r, fog->color.g, fog->color.b, std::max(fog->density, 0.0f));
		data.heightParameters = Vector4(std::max(fog->heightFalloff, 0.0f), height,
			std::max(fog->directionalExponent, 0.0f), std::max(fog->directionalIntensity, 0.0f));
		data.distanceParameters = Vector4(std::max(fog->startDistance, 0.0f),
			std::clamp(fog->maxOpacity, 0.0f, 1.0f), std::max(fog->cutoffDistance, 0.0f), 0.0f);
		data.directionalColor = Vector4(fog->directionalColor.r, fog->directionalColor.g,
			fog->directionalColor.b, std::max(fog->directionalStartDistance, 0.0f));
		data.secondaryParameters = Vector4(std::max(fog->secondDensity, 0.0f),
			std::max(fog->secondHeightFalloff, 0.0f), fog->secondHeightOffset, 0.0f);
		bool foundSun = false;
		if (atmosphere.aerialIndex != UINT32_MAX) {
			const auto& solar = atmosphere.solarLuminance;
			const auto& lunar = atmosphere.lunarLuminance;
			const bool moon = lunar.x + lunar.y + lunar.z > solar.x + solar.y + solar.z;
			const auto direction = moon ? atmosphere.moonDirection : atmosphere.sunDirection;
			data.sunDirection = Vector4(direction.x, direction.y, direction.z, 0.0f);
			foundSun = atmosphere.sunAvailable || moon;
		}
		context.world->ForEach<DirectionalLightComponent>([&](const Entity& entity, const auto& light) {
			if (foundSun || atmosphere.aerialIndex != UINT32_MAX || !LightExtract::IsVisible(*context.world, entity, light.enabled)) return;
			const Vector3 direction = LightExtract::GetDirectionalWorldDirection(*context.world, entity, light.direction);
			data.sunDirection = Vector4(direction.x, direction.y, direction.z, 0.0f);
			foundSun = true;
		});
		if (!foundSun) data.heightParameters.w = 0.0f;
		const auto sky = SceneSkyboxResolver::Resolve(graphics, context.assetDatabase, context.world,
			context.view->GetCullingMask(RenderCameraDomain::Perspective));
		data.irradianceIndex = sky.irradianceIndex;
		data.skyContribution = Vector4(sky.color.r, sky.color.g, sky.color.b,
			std::max(fog->skyContribution, 0.0f) * sky.iblIntensity);
	}
	if (context.resources && context.resources->GetSceneFinal()) {
		const auto* surface = context.resources->GetSceneFinal();
		data.viewportParameters = Vector4(1.0f / std::max(surface->GetWidth(), 1u),
			1.0f / std::max(surface->GetHeight(), 1u), 0.0f, 0.0f);
	}
	// 霧が無効なViewにも無効値を渡し他Viewの設定を持ち越さない
	constants_.Init(graphics.GetDXObject().GetResourceRetirement(), graphics.GetDXObject().GetDevice());
	constants_.Upload(data);
	context.bufferRegistry.Register({.alias = "HeightFogConstants", .resource = nullptr,
		.gpuAddress = constants_.GetGPUAddress()});
	if ((!fog && data.aerialIndex == UINT32_MAX) || !context.resources) return;
	auto* target = context.resources->GetSceneFinal();
	auto* main = context.resources->GetSceneMain();
	auto* depth = main ? main->GetDepthTexture() : nullptr;
	if (!target || !depth) return;
	auto* color = target->GetColorTexture(0);
	if (!color || !EnsurePipeline(graphics, color->GetResource()->GetDesc().Format)) return;
	auto* command = graphics.GetDXObject().GetDxCommand();
	auto* list = command->GetCommandList();
	depth->Transition(*command, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	color->Transition(*command, D3D12_RESOURCE_STATE_RENDER_TARGET);
	command->BindRenderTargets(std::optional<RenderTarget>(color->GetRenderTarget()), std::nullopt);
	command->SetViewportAndScissor(target->GetWidth(), target->GetHeight());
	list->SetGraphicsRootSignature(pipeline_->GetRootSignature());
	list->SetPipelineState(pipeline_->GetGraphicsPipeline(BlendMode::Normal));
	pipeline_->BindGlobalDescriptorTablesGraphics(list, graphics.GetSRVDescriptor().GetGPUHandle(0));
	bindings_.Sync(*pipeline_);
	RootBindingCommand::SetGraphicsCBV(list, bindings_.Get(constantSlot_), constants_.GetGPUAddress());
	RootBindingCommand::SetGraphicsSRV(list, bindings_.Get(depthSlot_), 0, depth->GetSRVGPUHandle());
	list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	list->DrawInstanced(3, 1, 0, 0);
}
