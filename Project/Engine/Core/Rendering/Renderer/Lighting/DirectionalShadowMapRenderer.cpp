#include "DirectionalShadowMapRenderer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPassExecutionHelper.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/Lighting/FrameLightBatch.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/Foundation/Utility/ScopedValue.h>

// c++
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

	constexpr uint32_t kShadowCascadeCount = Engine::DirectionalShadowMapState::kCascadeCount;
	constexpr uint32_t kShadowMapSize = 2048;

	// 光源空間の三軸
	struct ShadowLightAxes {

		Engine::Vector3 forward;
		Engine::Vector3 right;
		Engine::Vector3 up;
	};

	// 光源方向から共通の三軸を作成
	ShadowLightAxes BuildShadowLightAxes(const Engine::Vector3& direction) {

		const Engine::Vector3 forward = Engine::Vector3::NormalizeOr(direction, Engine::Vector3(0.0f, -1.0f, 0.0f));
		const Engine::Vector3 referenceUp =
			std::abs(forward.y) < 0.99f ? Engine::Vector3(0.0f, 1.0f, 0.0f) : Engine::Vector3(1.0f, 0.0f, 0.0f);
		const Engine::Vector3 right =
			Engine::Vector3::NormalizeOr(Engine::Vector3::Cross(referenceUp, forward), Engine::Vector3(1.0f, 0.0f, 0.0f));
		const Engine::Vector3 up =
			Engine::Vector3::NormalizeOr(Engine::Vector3::Cross(forward, right), Engine::Vector3(0.0f, 1.0f, 0.0f));
		return {forward, right, up};
	}

	// Cascadeの範囲をワールド座標へ展開
	std::array<Engine::Vector3, 8> BuildFrustumCorners(
		const Engine::ResolvedCameraView& camera, float nearDistance, float farDistance) {

		const Engine::Matrix4x4 inverseViewProjection = Engine::Matrix4x4::Inverse(camera.matrices.viewProjectionMatrix);
		std::array<Engine::Vector3, 4> nearCorners{};
		std::array<Engine::Vector3, 4> farCorners{};
		uint32_t index = 0;
		for (float y : {-1.0f, 1.0f}) {
			for (float x : {-1.0f, 1.0f}) {
				nearCorners[index] = Engine::Vector3::Transform(Engine::Vector3(x, y, 0.0f), inverseViewProjection);
				farCorners[index] = Engine::Vector3::Transform(Engine::Vector3(x, y, 1.0f), inverseViewProjection);
				++index;
			}
		}

		const float clipRange = (std::max)(camera.farClip - camera.nearClip, 0.001f);
		const float nearRatio = (nearDistance - camera.nearClip) / clipRange;
		const float farRatio = (farDistance - camera.nearClip) / clipRange;
		std::array<Engine::Vector3, 8> result{};
		for (uint32_t corner = 0; corner < 4; ++corner) {
			result[corner] = Engine::Vector3::Lerp(nearCorners[corner], farCorners[corner], nearRatio);
			result[corner + 4] = Engine::Vector3::Lerp(nearCorners[corner], farCorners[corner], farRatio);
		}
		return result;
	}

	// 光源位置と三軸からView行列を作成
	Engine::Matrix4x4 BuildLightView(const Engine::Vector3& center, const Engine::Vector3& direction, float distance) {

		const ShadowLightAxes axes = BuildShadowLightAxes(direction);
		const auto& forward = axes.forward;
		const auto& right = axes.right;
		const auto& up = axes.up;

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

	// 光源空間の移動を画素単位へ揃える
	Engine::Vector3 StabilizeShadowCenter(const Engine::Vector3& center, const Engine::Vector3& direction, float texelSize) {

		const ShadowLightAxes axes = BuildShadowLightAxes(direction);
		const auto& forward = axes.forward;
		const auto& right = axes.right;
		const auto& up = axes.up;

		// Light空間の中心をShadow Mapのtexelへ固定する
		const float stableRight = std::round(Engine::Vector3::Dot(center, right) / texelSize) * texelSize;
		const float stableUp = std::round(Engine::Vector3::Dot(center, up) / texelSize) * texelSize;
		const float forwardDistance = Engine::Vector3::Dot(center, forward);
		return right * stableRight + up * stableUp + forward * forwardDistance;
	}

	// 拡大率の最大値を取得
	float GetWorldMaxScale(const Engine::Matrix4x4& matrix) {

		const float scaleX = Engine::Vector3(matrix.m[0][0], matrix.m[0][1], matrix.m[0][2]).Length();
		const float scaleY = Engine::Vector3(matrix.m[1][0], matrix.m[1][1], matrix.m[1][2]).Length();
		const float scaleZ = Engine::Vector3(matrix.m[2][0], matrix.m[2][1], matrix.m[2][2]).Length();
		return (std::max)({scaleX, scaleY, scaleZ});
	}

	// Primitiveを包む球の半径を取得
	float GetPrimitiveBoundsRadius(const Engine::PrimitiveRendererComponent& renderer) {

		switch (renderer.type) {
		case Engine::PrimitiveType::Plane:
			return 0.5f *
				   std::sqrt(renderer.plane.size.x * renderer.plane.size.x + renderer.plane.size.y * renderer.plane.size.y);
		case Engine::PrimitiveType::CrossPlane:
			return 0.5f * std::sqrt(renderer.crossPlane.size.x * renderer.crossPlane.size.x +
									renderer.crossPlane.size.y * renderer.crossPlane.size.y);
		case Engine::PrimitiveType::Ring:
			return renderer.ring.outerRadius;
		case Engine::PrimitiveType::Cylinder: {
			const float radius =
				(std::max)({renderer.cylinder.topRadius, renderer.cylinder.centerRadius, renderer.cylinder.bottomRadius});
			return std::sqrt(radius * radius + renderer.cylinder.height * renderer.cylinder.height * 0.25f);
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

	// 遮蔽物の境界をワールド座標へ変換
	bool ResolveShadowCasterBounds(const Engine::RenderItem& item, const Engine::RenderSceneBatch& renderBatch,
		const Engine::MeshRenderBackend* meshBackend, Engine::Vector3& center, float& radius) {

		center = item.sortPosition;
		radius = GetWorldMaxScale(item.worldMatrix);
		if (item.backendID == Engine::RenderBackendID::Mesh && meshBackend) {

			const Engine::MeshRenderPayload* payload = renderBatch.GetPayload<Engine::MeshRenderPayload>(item);
			const Engine::MeshGPUResource* mesh = payload ? meshBackend->FindMeshResource(payload->mesh) : nullptr;
			if (!mesh) {
				return false;
			}
			center = Engine::Vector3::Transform(mesh->boundsCenter, item.worldMatrix);
			radius *= mesh->boundsRadius;
			return true;
		}
		if (item.backendID == Engine::RenderBackendID::Primitive) {

			const Engine::PrimitiveRenderPayload* payload = renderBatch.GetPayload<Engine::PrimitiveRenderPayload>(item);
			if (!payload || !payload->renderer) {
				return false;
			}
			radius *= GetPrimitiveBoundsRadius(*payload->renderer);
			return true;
		}
		return false;
	}

	// Cascadeへ影を落とす遮蔽物を深度範囲へ含める
	void ResolveShadowDepthRange(std::span<const Engine::RenderItem* const> shadowItems,
		const Engine::RenderSceneBatch& renderBatch, const Engine::MeshRenderBackend* meshBackend,
		const Engine::Vector3& center, const Engine::Vector3& direction, float cascadeRadius, float& lightDistance,
		float& depthRange) {

		const ShadowLightAxes axes = BuildShadowLightAxes(direction);
		const auto& forward = axes.forward;
		const auto& right = axes.right;
		const auto& up = axes.up;

		float minimumDepth = -cascadeRadius;
		float maximumDepth = cascadeRadius;
		for (const Engine::RenderItem* item : shadowItems) {

			Engine::Vector3 casterCenter{};
			float casterRadius = 0.0f;
			if (!item || !ResolveShadowCasterBounds(*item, renderBatch, meshBackend, casterCenter, casterRadius)) {
				continue;
			}
			const Engine::Vector3 offset = casterCenter - center;
			if (cascadeRadius + casterRadius < std::abs(Engine::Vector3::Dot(offset, right)) ||
				cascadeRadius + casterRadius < std::abs(Engine::Vector3::Dot(offset, up))) {

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

}

bool Engine::DirectionalShadowMapRenderer::Render(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
	SceneExecutionContext& context, const RenderPipelineDeps& deps) {

	state_.lightIndex = UINT32_MAX;
	if (!context.view || !context.viewLights || !deps.renderBatch) {
		return false;
	}
	const ResolvedCameraView* camera = context.view->FindCamera(RenderCameraDomain::Perspective);
	if (!camera || !camera->valid) {
		return false;
	}

	const DirectionalLightItem* shadowLight = nullptr;
	for (uint32_t index = 0; index < context.viewLights->directionalLights.size(); ++index) {

		const DirectionalLightItem* candidate = context.viewLights->directionalLights[index];
		if (candidate && candidate->shadowStrength > 0.0f) {
			shadowLight = candidate;
			state_.lightIndex = index;
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

		if (!item || (item->backendID != RenderBackendID::Mesh && item->backendID != RenderBackendID::Primitive) ||
			!item->castShadows) {

			continue;
		}
		shadowItems.emplace_back(item);
	}
	if (shadowItems.empty()) {
		return false;
	}
	const MeshRenderBackend* meshBackend =
		deps.backendRegistry ? dynamic_cast<const MeshRenderBackend*>(deps.backendRegistry->Find(RenderBackendID::Mesh))
							 : nullptr;

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
		shadowMap->Create(graphicsCore.GetDXObject().GetDevice(), &graphicsCore.GetRTVDescriptor(),
			&graphicsCore.GetDSVDescriptor(), &graphicsCore.GetSRVDescriptor(), desc);
	}

	const float nearClip = (std::max)(camera->nearClip, 0.01f);
	const float farClip = (std::min)(camera->farClip, 500.0f);
	float cascadeNear = nearClip;
	for (uint32_t cascade = 0; cascade < kShadowCascadeCount; ++cascade) {

		const float splitRatio = static_cast<float>(cascade + 1) / static_cast<float>(kShadowCascadeCount);
		const float logarithmic = nearClip * std::pow(farClip / nearClip, splitRatio);
		const float uniform = nearClip + (farClip - nearClip) * splitRatio;
		const float cascadeFar = std::lerp(uniform, logarithmic, 0.65f);

		const std::array<Vector3, 8> corners = BuildFrustumCorners(*camera, cascadeNear, cascadeFar);
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
		const float texelSize = radius * 2.0f / static_cast<float>(kShadowMapSize);
		const Vector3 stableCenter = StabilizeShadowCenter(center, shadowLight->direction, texelSize);
		float lightDistance = 0.0f;
		float depthRange = 0.0f;
		ResolveShadowDepthRange(shadowItems, *deps.renderBatch, meshBackend, stableCenter, shadowLight->direction, radius,
			lightDistance, depthRange);
		const Matrix4x4 lightView = BuildLightView(stableCenter, shadowLight->direction, lightDistance);
		const Matrix4x4 projection = Matrix4x4::MakeOrthographicMatrix(-radius, radius, radius, -radius, 0.1f, depthRange);
		state_.viewProjections[cascade] = lightView * projection;
		(&state_.cascadeSplits.x)[cascade] = cascadeFar;
		(&state_.depthRanges.x)[cascade] = depthRange;
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
		shadowView.perspective.matrices.viewProjectionMatrix = state_.viewProjections[cascade];
		shadowView.perspective.matrices.inverseViewMatrix = Matrix4x4::Inverse(lightView);
		shadowView.perspective.matrices.inverseProjectionMatrix = Matrix4x4::Inverse(projection);
		shadowView.perspective.cameraPos = shadowView.perspective.matrices.inverseViewMatrix.GetTranslationValue();
		shadowView.perspective.forward = Vector3::NormalizeOr(shadowLight->direction, Vector3(0.0f, -1.0f, 0.0f));
		shadowView.perspective.nearClip = 0.1f;
		shadowView.perspective.farClip = depthRange;
		shadowView.perspective.cullingMask = 0xffffffffu;

		MultiRenderTarget* target = shadowMaps_[cascade].get();
		target->TransitionForRender(*graphicsCore.GetDXObject().GetDxCommand());
		target->Clear(*graphicsCore.GetDXObject().GetDxCommand(), MultiRenderTargetClearDesc{
																	  .clearColor = false,
																	  .clearDepth = true,
																	  .clearDepthValue = 1.0f,
																	  .clearStencil = false,
																  });

		// 描画に失敗しても元のCameraと描画設定へ戻す
		ScopedValue lodView(context.lodView, context.view);
		ScopedValue renderView(context.view, &shadowView);
		ScopedValue cullingView(context.cullingView, &shadowView);
		ScopedValue viewport(context.useViewportRect, false);
		ScopedValue disableCulling(context.disableMeshCulling, true);
		ScopedValue twoSided(context.forceTwoSidedRasterizer, true);
		ScopedValue disableLODDither(context.disableLODDither, true);
		RenderPassExecutionHelper::Execute(
			graphicsCore, context, shadowItems, deps, target, MaterialPassKind::ZPrepass, false, true);
	}
	return true;
}

Engine::DepthTexture2D* Engine::DirectionalShadowMapRenderer::GetDepthTexture(uint32_t cascade) {

	return shadowMaps_[cascade] ? shadowMaps_[cascade]->GetDepthTexture() : nullptr;
}
