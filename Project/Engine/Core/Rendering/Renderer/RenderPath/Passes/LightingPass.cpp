#include "LightingPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/PipelineStateBuilder.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPassExecutionHelper.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTexture2D.h>
#include <Engine/Core/Rendering/Renderer/Lighting/SceneSkyboxResolver.h>
#include <Engine/Core/Rendering/Renderer/Lighting/FrameLightBatch.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>

// c++
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

	constexpr uint32_t kShadowCascadeCount = 4;
	constexpr uint32_t kShadowMapSize = 2048;

	std::array<Engine::Vector3, 8> BuildFrustumCorners(
		const Engine::ResolvedCameraView& camera, float nearDistance,
		float farDistance) {

		const Engine::Matrix4x4 inverseViewProjection =
			Engine::Matrix4x4::Inverse(camera.matrices.viewProjectionMatrix);
		std::array<Engine::Vector3, 4> nearCorners{};
		std::array<Engine::Vector3, 4> farCorners{};
		uint32_t index = 0;
		for (float y : { -1.0f, 1.0f }) {
			for (float x : { -1.0f, 1.0f }) {
				nearCorners[index] = Engine::Vector3::Transform(
					Engine::Vector3(x, y, 0.0f), inverseViewProjection);
				farCorners[index] = Engine::Vector3::Transform(
					Engine::Vector3(x, y, 1.0f), inverseViewProjection);
				++index;
			}
		}

		const float clipRange = (std::max)(camera.farClip - camera.nearClip, 0.001f);
		const float nearRatio = (nearDistance - camera.nearClip) / clipRange;
		const float farRatio = (farDistance - camera.nearClip) / clipRange;
		std::array<Engine::Vector3, 8> result{};
		for (uint32_t corner = 0; corner < 4; ++corner) {
			result[corner] = Engine::Vector3::Lerp(
				nearCorners[corner], farCorners[corner], nearRatio);
			result[corner + 4] = Engine::Vector3::Lerp(
				nearCorners[corner], farCorners[corner], farRatio);
		}
		return result;
	}

	Engine::Matrix4x4 BuildLightView(const Engine::Vector3& center,
		const Engine::Vector3& direction, float distance) {

		const Engine::Vector3 forward = Engine::Vector3::NormalizeOr(
			direction, Engine::Vector3(0.0f, -1.0f, 0.0f));
		const Engine::Vector3 referenceUp = std::abs(forward.y) < 0.99f ?
			Engine::Vector3(0.0f, 1.0f, 0.0f) :
			Engine::Vector3(1.0f, 0.0f, 0.0f);
		const Engine::Vector3 right = Engine::Vector3::NormalizeOr(
			Engine::Vector3::Cross(referenceUp, forward),
			Engine::Vector3(1.0f, 0.0f, 0.0f));
		const Engine::Vector3 up = Engine::Vector3::NormalizeOr(
			Engine::Vector3::Cross(forward, right),
			Engine::Vector3(0.0f, 1.0f, 0.0f));

		Engine::Matrix4x4 world = Engine::Matrix4x4::Identity();
		world.m[0][0] = right.x;
		world.m[0][1] = right.y;
		world.m[0][2] = right.z;
		world.m[1][0] = up.x;
		world.m[1][1] = up.y;
		world.m[1][2] = up.z;
		world.m[2][0] = forward.x;
		world.m[2][1] = forward.y;
		world.m[2][2] = forward.z;
		const Engine::Vector3 position = center - forward * distance;
		world.m[3][0] = position.x;
		world.m[3][1] = position.y;
		world.m[3][2] = position.z;
		return Engine::Matrix4x4::Inverse(world);
	}

	Engine::Vector3 StabilizeShadowCenter(const Engine::Vector3& center,
		const Engine::Vector3& direction, float texelSize) {

		const Engine::Vector3 forward = Engine::Vector3::NormalizeOr(
			direction, Engine::Vector3(0.0f, -1.0f, 0.0f));
		const Engine::Vector3 referenceUp = std::abs(forward.y) < 0.99f ?
			Engine::Vector3(0.0f, 1.0f, 0.0f) :
			Engine::Vector3(1.0f, 0.0f, 0.0f);
		const Engine::Vector3 right = Engine::Vector3::NormalizeOr(
			Engine::Vector3::Cross(referenceUp, forward),
			Engine::Vector3(1.0f, 0.0f, 0.0f));
		const Engine::Vector3 up = Engine::Vector3::NormalizeOr(
			Engine::Vector3::Cross(forward, right),
			Engine::Vector3(0.0f, 1.0f, 0.0f));

		// Light空間の中心をShadow Mapのtexelへ固定する
		const float stableRight = std::round(
			Engine::Vector3::Dot(center, right) / texelSize) * texelSize;
		const float stableUp = std::round(
			Engine::Vector3::Dot(center, up) / texelSize) * texelSize;
		const float forwardDistance = Engine::Vector3::Dot(center, forward);
		return right * stableRight + up * stableUp +
			forward * forwardDistance;
	}

	float GetWorldMaxScale(const Engine::Matrix4x4& matrix) {

		const float scaleX = Engine::Vector3(
			matrix.m[0][0], matrix.m[0][1], matrix.m[0][2]).Length();
		const float scaleY = Engine::Vector3(
			matrix.m[1][0], matrix.m[1][1], matrix.m[1][2]).Length();
		const float scaleZ = Engine::Vector3(
			matrix.m[2][0], matrix.m[2][1], matrix.m[2][2]).Length();
		return (std::max)({ scaleX, scaleY, scaleZ });
	}

	float GetPrimitiveBoundsRadius(
		const Engine::PrimitiveRendererComponent& renderer) {

		switch (renderer.type) {
		case Engine::PrimitiveType::Plane:
			return 0.5f * std::sqrt(
				renderer.plane.size.x * renderer.plane.size.x +
				renderer.plane.size.y * renderer.plane.size.y);
		case Engine::PrimitiveType::CrossPlane:
			return 0.5f * std::sqrt(
				renderer.crossPlane.size.x * renderer.crossPlane.size.x +
				renderer.crossPlane.size.y * renderer.crossPlane.size.y);
		case Engine::PrimitiveType::Ring:
			return renderer.ring.outerRadius;
		case Engine::PrimitiveType::Cylinder: {
			const float radius = (std::max)({
				renderer.cylinder.topRadius,
				renderer.cylinder.centerRadius,
				renderer.cylinder.bottomRadius });
			return std::sqrt(radius * radius +
				renderer.cylinder.height * renderer.cylinder.height * 0.25f);
		}
		case Engine::PrimitiveType::Sphere:
			return renderer.sphere.radius;
		case Engine::PrimitiveType::Hemisphere:
			return renderer.hemisphere.radius;
		case Engine::PrimitiveType::Cube:
			return 0.5f * renderer.cube.size.Length();
		}
		return 1.0f;
	}

	bool ResolveShadowCasterBounds(const Engine::RenderItem& item,
		const Engine::RenderSceneBatch& renderBatch,
		const Engine::MeshRenderBackend* meshBackend,
		Engine::Vector3& center, float& radius) {

		center = item.sortPosition;
		radius = GetWorldMaxScale(item.worldMatrix);
		if (item.backendID == Engine::RenderBackendID::Mesh && meshBackend) {

			const Engine::MeshRenderPayload* payload =
				renderBatch.GetPayload<Engine::MeshRenderPayload>(item);
			const Engine::MeshGPUResource* mesh = payload ?
				meshBackend->FindMeshResource(payload->mesh) : nullptr;
			if (!mesh) {
				return false;
			}
			center = Engine::Vector3::Transform(
				mesh->boundsCenter, item.worldMatrix);
			radius *= mesh->boundsRadius;
			return true;
		}
		if (item.backendID == Engine::RenderBackendID::Primitive) {

			const Engine::PrimitiveRenderPayload* payload =
				renderBatch.GetPayload<Engine::PrimitiveRenderPayload>(item);
			if (!payload || !payload->renderer) {
				return false;
			}
			radius *= GetPrimitiveBoundsRadius(*payload->renderer);
			return true;
		}
		return false;
	}

	void ResolveShadowDepthRange(
		std::span<const Engine::RenderItem* const> shadowItems,
		const Engine::RenderSceneBatch& renderBatch,
		const Engine::MeshRenderBackend* meshBackend,
		const Engine::Vector3& center, const Engine::Vector3& direction,
		float cascadeRadius, float& lightDistance, float& depthRange) {

		const Engine::Vector3 forward = Engine::Vector3::NormalizeOr(
			direction, Engine::Vector3(0.0f, -1.0f, 0.0f));
		const Engine::Vector3 referenceUp = std::abs(forward.y) < 0.99f ?
			Engine::Vector3(0.0f, 1.0f, 0.0f) :
			Engine::Vector3(1.0f, 0.0f, 0.0f);
		const Engine::Vector3 right = Engine::Vector3::NormalizeOr(
			Engine::Vector3::Cross(referenceUp, forward),
			Engine::Vector3(1.0f, 0.0f, 0.0f));
		const Engine::Vector3 up = Engine::Vector3::NormalizeOr(
			Engine::Vector3::Cross(forward, right),
			Engine::Vector3(0.0f, 1.0f, 0.0f));

		float minimumDepth = -cascadeRadius;
		float maximumDepth = cascadeRadius;
		for (const Engine::RenderItem* item : shadowItems) {

			Engine::Vector3 casterCenter{};
			float casterRadius = 0.0f;
			if (!item || !ResolveShadowCasterBounds(
				*item, renderBatch, meshBackend, casterCenter, casterRadius)) {
				continue;
			}
			const Engine::Vector3 offset = casterCenter - center;
			if (cascadeRadius + casterRadius <
				std::abs(Engine::Vector3::Dot(offset, right)) ||
				cascadeRadius + casterRadius <
				std::abs(Engine::Vector3::Dot(offset, up))) {

				continue;
			}
			const float casterDepth = Engine::Vector3::Dot(offset, forward);
			minimumDepth = (std::min)(minimumDepth, casterDepth - casterRadius);
			maximumDepth = (std::max)(maximumDepth, casterDepth + casterRadius);
		}

		// Cascade内へ影を落とせる遮蔽物までLight Cameraへ含める
		const float padding = (std::max)(cascadeRadius * 0.05f, 5.0f);
		lightDistance = -minimumDepth + padding;
		depthRange = (std::max)(maximumDepth - minimumDepth + padding * 2.0f, 1.0f);
	}

	bool HasRaytracingReflection(
		const Engine::SceneExecutionContext& context) {

		if (!context.renderExtensionRuntime) {
			return false;
		}
		const bool gameView = context.kind == Engine::RenderViewKind::Game;
		const Engine::RenderFeatureProfileRuntime& runtime =
			*context.renderExtensionRuntime;
		for (const Engine::RenderFeaturePassSettings& pass :
			runtime.GetProfile().passes) {

			if (pass.enabled &&
				pass.type == Engine::RenderFeaturePassType::RayTracing &&
				pass.material == Engine::BuiltinAssets::Materials::RaytracingReflection &&
				(gameView ? pass.gameView : pass.sceneView) &&
				runtime.IsPassHierarchyEnabled(pass.id)) {

				return true;
			}
		}
		return false;
	}
}

//============================================================================
//	LightingPass classMethods
//============================================================================

Engine::LightingPass::LightingPass(const RenderPipelineDeps& deps) :
	deps_(deps) {

	// GBufferの各アタッチメントをregister順でスロット登録する、t0-t5はSceneMainのcolor並びと一致
	albedoSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::SRV, 0, 0);
	normalSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::SRV, 1, 0);
	worldPosSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::SRV, 2, 0);
	materialSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::SRV, 3, 0);
	emissiveSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::SRV, 4, 0);
	flagsSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::SRV, 5, 0);
	constantsSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::CBV, 1, 0);
	for (uint32_t cascade = 0; cascade < kShadowCascadeCount; ++cascade) {
		shadowMapSlots_[cascade] = bindCache_.AddSlotByRegister(
			ShaderBindingKind::SRV, 13 + cascade, 0);
	}
}

void Engine::LightingPass::EnsurePipeline(GraphicsCore& graphicsCore, DXGI_FORMAT colorFormat) {

	if (initialized_) {
		return;
	}

	ID3D12Device8* device = graphicsCore.GetDXObject().GetDevice();
	DxShaderCompiler* compiler = graphicsCore.GetDXObject().GetDxShaderCompiler();

	GraphicsPipelineDesc desc{};
	desc.type = PipelineType::Vertex;

	// 画面全体を覆う共通VSを使い回す
	desc.preRaster.file = "Builtin/FullscreenCopy/fullscreenCopy.VS.hlsl";
	desc.preRaster.shader = BuiltinAssets::Shaders::FullscreenCopy;
	desc.preRaster.entry = "main";
	desc.preRaster.profile = "vs_6_0";

	// 通常LightingはDescriptor Table経由でSM6.0へ対応する
	desc.pixel.file = "Builtin/Lighting/deferredLighting.PS.hlsl";
	desc.pixel.shader = BuiltinAssets::Shaders::DeferredLighting;
	desc.pixel.entry = "main";
	desc.pixel.profile = "ps_6_0";

	// cubemap用の静的サンプラー
	D3D12_STATIC_SAMPLER_DESC sampler{};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderRegister = 0;
	sampler.RegisterSpace = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	desc.staticSamplers.push_back(sampler);

	desc.rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	desc.rasterizer.CullMode = D3D12_CULL_MODE_NONE;

	// 全画面合成なので深度は使わない
	desc.depthStencil = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	desc.depthStencil.DepthEnable = FALSE;
	desc.depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	desc.depthStencil.StencilEnable = FALSE;

	desc.sampleDesc = { 1, 0 };
	desc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	desc.numRenderTargets = 1;
	desc.rtvFormats[0] = colorFormat;
	desc.dsvFormat = DXGI_FORMAT_UNKNOWN;

	// シャドウ無し版、gSceneTLASを参照しない
	desc.pixel.entry = "main";
	initialized_ = (pipeline_ = PipelineStateBuilder::CreateGraphics(graphicsCore.GetDXObject().GetResourceRetirement(), device, compiler, desc)) != nullptr;

	// TLASシャドウ付き版、inlineRT非対応環境ではPSO構築に失敗するためフラグで持つ
	desc.pixel.entry = "mainShadowed";
	desc.pixel.shader = BuiltinAssets::Shaders::DeferredLightingShadowed;
	desc.pixel.profile = "ps_6_6";
	shadowedAvailable_ = (pipelineShadowed_ = PipelineStateBuilder::CreateGraphics(
		graphicsCore.GetDXObject().GetResourceRetirement(), device, compiler, desc)) != nullptr;
}

Engine::DxConstBuffer<Engine::LightingPass::LightingConstants>& Engine::LightingPass::AllocateConstantBuffer(
	GraphicsCore& graphicsCore) {

	const uint32_t frameIndex = GraphicsFrameState::GetCurrentIndex();
	const uint64_t frameSerial = GraphicsFrameState::GetFrameSerial();
	if (constantBufferFrameSerials_[frameIndex] != frameSerial) {
		constantBufferFrameSerials_[frameIndex] = frameSerial;
		constantBufferIndices_[frameIndex] = 0;
	}
	auto& buffers = constantBuffers_[frameIndex];
	uint32_t& bufferIndex = constantBufferIndices_[frameIndex];
	if (buffers.size() <= bufferIndex) {

		auto buffer = std::make_unique<DxConstBuffer<LightingConstants>>();
		buffer->CreateBuffer(graphicsCore.GetDXObject().GetResourceRetirement(), graphicsCore.GetDXObject().GetDevice());
		buffers.push_back(std::move(buffer));
	}
	return *buffers[bufferIndex++];
}

void Engine::LightingPass::BindGBufferSRV(ID3D12GraphicsCommandList* commandList,
	PipelineBindingCache::SlotID slot, RenderTexture2D* texture) {

	if (!bindCache_.Has(slot) || !texture) {
		return;
	}
	RootBindingCommand::SetGraphicsSRV(commandList, bindCache_.Get(slot), 0, texture->GetSRVGPUHandle());
}

bool Engine::LightingPass::RenderDirectionalShadowMaps(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	shadowMapLightIndex_ = UINT32_MAX;
	if (!context.view || !context.viewLights || !deps_.renderBatch) {
		return false;
	}
	const ResolvedCameraView* camera =
		context.view->FindCamera(RenderCameraDomain::Perspective);
	if (!camera || !camera->valid) {
		return false;
	}

	const DirectionalLightItem* shadowLight = nullptr;
	for (uint32_t index = 0;
		index < context.viewLights->directionalLights.size(); ++index) {

		const DirectionalLightItem* candidate =
			context.viewLights->directionalLights[index];
		if (candidate && candidate->shadowStrength > 0.0f) {
			shadowLight = candidate;
			shadowMapLightIndex_ = index;
			break;
		}
	}
	if (!shadowLight) {
		return false;
	}

	std::vector<const RenderItem*> shadowItems{};
	const RenderPassItemList& opaque = passBuckets.Get(RenderPhase::Opaque);
	shadowItems.reserve(opaque.items.size());
	for (const RenderItem* item : opaque.items) {

		if (!item ||
			(item->backendID != RenderBackendID::Mesh &&
				item->backendID != RenderBackendID::Primitive) ||
			!item->castShadows ||
			(item->renderingLayerMask &
				shadowLight->common.shadowLayerMask) == 0) {

			continue;
		}
		shadowItems.emplace_back(item);
	}
	if (shadowItems.empty()) {
		return false;
	}
	const MeshRenderBackend* meshBackend = deps_.backendRegistry ?
		dynamic_cast<const MeshRenderBackend*>(
			deps_.backendRegistry->Find(RenderBackendID::Mesh)) : nullptr;

	for (std::unique_ptr<MultiRenderTarget>& shadowMap : shadowMaps_) {
		if (shadowMap) {
			continue;
		}

		MultiRenderTargetCreateDesc desc{};
		desc.width = kShadowMapSize;
		desc.height = kShadowMapSize;
		DepthTextureCreateDesc depth{};
		depth.width = kShadowMapSize;
		depth.height = kShadowMapSize;
		depth.resourceFormat = DXGI_FORMAT_R32_TYPELESS;
		depth.dsvFormat = DXGI_FORMAT_D32_FLOAT;
		depth.srvFormat = DXGI_FORMAT_R32_FLOAT;
		depth.debugName = L"DirectionalShadowMap";
		desc.depth = depth;

		shadowMap = std::make_unique<MultiRenderTarget>();
		shadowMap->Create(graphicsCore.GetDXObject().GetDevice(),
			&graphicsCore.GetRTVDescriptor(), &graphicsCore.GetDSVDescriptor(),
			&graphicsCore.GetSRVDescriptor(), desc);
	}

	const float nearClip = (std::max)(camera->nearClip, 0.01f);
	const float farClip = (std::min)(camera->farClip, 500.0f);
	float cascadeNear = nearClip;
	for (uint32_t cascade = 0; cascade < kShadowCascadeCount; ++cascade) {

		const float splitRatio =
			static_cast<float>(cascade + 1) /
			static_cast<float>(kShadowCascadeCount);
		const float logarithmic = nearClip *
			std::pow(farClip / nearClip, splitRatio);
		const float uniform = nearClip +
			(farClip - nearClip) * splitRatio;
		const float cascadeFar =
			std::lerp(uniform, logarithmic, 0.65f);

		const std::array<Vector3, 8> corners =
			BuildFrustumCorners(*camera, cascadeNear, cascadeFar);
		Vector3 center = Vector3::AnyInit(0.0f);
		for (const Vector3& corner : corners) {
			center += corner;
		}
		center /= static_cast<float>(corners.size());

		float radius = 0.0f;
		for (const Vector3& corner : corners) {
			radius = (std::max)(radius, (corner - center).Length());
		}
		radius = (std::max)(std::ceil(radius * 16.0f) / 16.0f, 1.0f);
		const float texelSize = radius * 2.0f /
			static_cast<float>(kShadowMapSize);
		const Vector3 stableCenter = StabilizeShadowCenter(
			center, shadowLight->direction, texelSize);
		float lightDistance = 0.0f;
		float depthRange = 0.0f;
		ResolveShadowDepthRange(shadowItems, *deps_.renderBatch,
			meshBackend, stableCenter, shadowLight->direction,
			radius, lightDistance, depthRange);
		const Matrix4x4 lightView = BuildLightView(
			stableCenter, shadowLight->direction, lightDistance);
		const Matrix4x4 projection = Matrix4x4::MakeOrthographicMatrix(
			-radius, radius, radius, -radius,
			0.1f, depthRange);
		shadowViewProjections_[cascade] = lightView * projection;
		(&shadowCascadeSplits_.x)[cascade] = cascadeFar;
		(&shadowDepthRanges_.x)[cascade] = depthRange;
		cascadeNear = cascadeFar;

		ResolvedRenderView shadowView = *context.view;
		shadowView.width = kShadowMapSize;
		shadowView.height = kShadowMapSize;
		shadowView.outputX = 0;
		shadowView.outputY = 0;
		shadowView.outputWidth = kShadowMapSize;
		shadowView.outputHeight = kShadowMapSize;
		shadowView.normalizedOutputX = 0.0f;
		shadowView.normalizedOutputY = 0.0f;
		shadowView.normalizedOutputWidth = 1.0f;
		shadowView.normalizedOutputHeight = 1.0f;
		shadowView.targetTexture = {};
		shadowView.orthographic = {};
		shadowView.perspective = {};
		shadowView.perspective.valid = true;
		shadowView.perspective.projectionMode = ResolvedProjectionMode::Orthographic;
		shadowView.perspective.matrices.viewMatrix = lightView;
		shadowView.perspective.matrices.projectionMatrix = projection;
		shadowView.perspective.matrices.viewProjectionMatrix =
			shadowViewProjections_[cascade];
		shadowView.perspective.matrices.inverseViewMatrix =
			Matrix4x4::Inverse(lightView);
		shadowView.perspective.matrices.inverseProjectionMatrix =
			Matrix4x4::Inverse(projection);
		shadowView.perspective.cameraPos =
			shadowView.perspective.matrices.inverseViewMatrix.GetTranslationValue();
		shadowView.perspective.forward =
			Vector3::NormalizeOr(shadowLight->direction,
				Vector3(0.0f, -1.0f, 0.0f));
		shadowView.perspective.nearClip = 0.1f;
		shadowView.perspective.farClip = depthRange;
		shadowView.perspective.cullingMask =
			shadowLight->common.shadowLayerMask;

		MultiRenderTarget* target = shadowMaps_[cascade].get();
		target->TransitionForRender(*graphicsCore.GetDXObject().GetDxCommand());
		target->Clear(*graphicsCore.GetDXObject().GetDxCommand(),
			MultiRenderTargetClearDesc{
				.clearColor = false,
				.clearDepth = true,
				.clearDepthValue = 1.0f,
				.clearStencil = false,
			});

		const ResolvedRenderView* previousView = context.view;
		const ResolvedRenderView* previousCullingView = context.cullingView;
		const ResolvedRenderView* previousLODView = context.lodView;
		const bool previousViewport = context.useViewportRect;
		const bool previousDisableCulling = context.disableMeshCulling;
		const bool previousTwoSided = context.forceTwoSidedRasterizer;
		const bool previousLODDither = context.disableLODDither;
		context.view = &shadowView;
		context.cullingView = &shadowView;
		context.lodView = previousView;
		context.useViewportRect = false;
		context.disableMeshCulling = true;
		context.forceTwoSidedRasterizer = true;
		context.disableLODDither = true;
		RenderPassExecutionHelper::Execute(graphicsCore, context,
			shadowItems, deps_, target, MaterialPassKind::ZPrepass, false, true);
		context.view = previousView;
		context.cullingView = previousCullingView;
		context.lodView = previousLODView;
		context.useViewportRect = previousViewport;
		context.disableMeshCulling = previousDisableCulling;
		context.forceTwoSidedRasterizer = previousTwoSided;
		context.disableLODDither = previousLODDither;
	}
	return true;
}

void Engine::LightingPass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	if (!context.resources || !context.view) {
		return;
	}

	// GBufferと合成先が揃っていなければ描けない
	MultiRenderTarget* sceneMain = context.resources->GetSceneMain();
	MultiRenderTarget* sceneFinal = context.resources->GetSceneFinal();
	if (!sceneMain || !sceneFinal) {
		return;
	}
	RenderTexture2D* destColor = sceneFinal->GetColorTexture(0);
	if (!destColor) {
		return;
	}

	EnsurePipeline(graphicsCore, destColor->GetFormat());
	if (!initialized_) {
		return;
	}
	const bool shadowMapAvailable =
		RenderDirectionalShadowMaps(graphicsCore, passBuckets, context);

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	auto* commandList = dxCommand->GetCommandList();

	// GBufferをシェーダーリードへ、SceneFinalをレンダーターゲットへ遷移してバインド
	sceneMain->TransitionForShaderRead(*dxCommand);
	sceneFinal->TransitionForRender(*dxCommand);
	sceneFinal->Bind(*dxCommand);

	dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });

	// 背景に使うskyboxを探す、最初に見つかったskyboxが対象
	const SceneSkyboxInfo skybox = SceneSkyboxResolver::Resolve(graphicsCore, context.assetDatabase, context.world);
	uint32_t irradianceCubemapIndex = 0xFFFFFFFF;
	if (skybox.found) {

		// 拡散IBL用の放射照度cubemapを更新する、描画PSO設定前にコンピュートを積む
		irradianceMap_.Update(graphicsCore, skybox.cubemapAssetID, skybox.cubemapIndex);
		irradianceCubemapIndex = irradianceMap_.GetSRVIndex();
	}

	// 影付きライトがありinlineRTとTLASを使えるときだけシャドウ付きPSOを選ぶ
	const auto& runtimeFeatures = graphicsCore.GetDXObject().GetFeatureController().GetRuntimeFeatures();
	const bool tlasAvailable = context.bufferRegistry.Find("gSceneTLAS") != nullptr;
	const bool useShadow = context.hasShadowCastingLight &&
		shadowedAvailable_ && runtimeFeatures.useInlineRayTracing &&
		tlasAvailable;
	PipelineState& activePipeline = useShadow ? *pipelineShadowed_ : *pipeline_;

	commandList->SetGraphicsRootSignature(activePipeline.GetRootSignature());
	commandList->SetPipelineState(activePipeline.GetGraphicsPipeline(BlendMode::Normal));
	activePipeline.BindGlobalDescriptorTablesGraphics(commandList,
		graphicsCore.GetSRVDescriptor().GetGPUHandle(0));

	// ライトバッファとTLASを名前でバインド
	registryAutoBindTable_.Sync(activePipeline, context.bufferRegistry);
	registryAutoBindTable_.BindGraphics(context.bufferRegistry, commandList);

	// GBufferの各SRVをバインドする
	bindCache_.Sync(activePipeline);
	BindGBufferSRV(commandList, albedoSlot_, sceneMain->GetColorTexture(0));
	BindGBufferSRV(commandList, normalSlot_, sceneMain->GetColorTexture(1));
	BindGBufferSRV(commandList, worldPosSlot_, sceneMain->GetColorTexture(2));
	BindGBufferSRV(commandList, materialSlot_, sceneMain->GetColorTexture(3));
	BindGBufferSRV(commandList, emissiveSlot_, sceneMain->GetColorTexture(4));
	BindGBufferSRV(commandList, flagsSlot_, sceneMain->GetColorTexture(5));
	for (uint32_t cascade = 0; cascade < kShadowCascadeCount; ++cascade) {

		DepthTexture2D* shadowDepth = shadowMaps_[cascade] ?
			shadowMaps_[cascade]->GetDepthTexture() : nullptr;
		if (!shadowDepth || !bindCache_.Has(shadowMapSlots_[cascade])) {
			continue;
		}
		shadowDepth->Transition(*dxCommand,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		RootBindingCommand::SetGraphicsSRV(commandList,
			bindCache_.Get(shadowMapSlots_[cascade]), 0,
			shadowDepth->GetSRVGPUHandle());
	}

	// 背景復元用の逆ビュー射影と視点を集める、2Dビューでは背景を出さない
	LightingConstants constants{};
	constants.ambientIntensity = 0.03f;
	constants.skyboxColor = skybox.found ? skybox.color : Color4::FromHex(0x303030ff);
	constants.skyboxCubemapIndex = skybox.found ? skybox.cubemapIndex : 0xFFFFFFFF;
	constants.hasSkybox = skybox.found ? 1u : 0u;
	constants.irradianceCubemapIndex = irradianceCubemapIndex;
	constants.iblIntensity = skybox.iblIntensity;
	constants.softShadowSampleCount =
		runtimeFeatures.softShadowSampleCount;
	constants.shadowMapAvailable = shadowMapAvailable ? 1u : 0u;
	constants.shadowMapLightIndex = shadowMapLightIndex_;
	constants.reflectionFeatureActive =
		runtimeFeatures.useDispatchRays && tlasAvailable &&
		HasRaytracingReflection(context) ? 1u : 0u;
	constants.shadowViewProjections = shadowViewProjections_;
	constants.shadowCascadeSplits = shadowCascadeSplits_;
	constants.shadowDepthRanges = shadowDepthRanges_;
	constants.viewportWidth = sceneFinal->GetWidth();
	constants.viewportHeight = sceneFinal->GetHeight();

	const ResolvedCameraView* camera = context.view->FindCamera(RenderCameraDomain::Perspective);
	if (camera && camera->valid) {

		constants.cameraPos = camera->cameraPos;
		constants.inverseViewProjection =
			camera->matrices.inverseProjectionMatrix * camera->matrices.inverseViewMatrix;
		constants.viewMatrix = camera->matrices.viewMatrix;
		constants.viewProjectionMatrix = camera->matrices.viewProjectionMatrix;
	} else {

		// 透視カメラが無い場合はskyboxを出さずambientと発光だけにする
		constants.hasSkybox = 0;
	}

	DxConstBuffer<LightingConstants>& buffer = AllocateConstantBuffer(graphicsCore);
	buffer.TransferData(constants);
	if (bindCache_.Has(constantsSlot_)) {

		RootBindingCommand::SetGraphicsCBV(commandList, bindCache_.Get(constantsSlot_), buffer.GetResource()->GetGPUVirtualAddress());
	}

	// 画面全体の三角形を描いてGBufferを合成する
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(3, 1, 0, 0);
}
