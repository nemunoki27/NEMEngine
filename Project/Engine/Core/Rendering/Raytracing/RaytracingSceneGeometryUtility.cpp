#include "RaytracingSceneGeometryUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Materials/MaterialResolver.h>
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/RenderBillboardUtility.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/Rendering/Primitive/PrimitiveGeometryManager.h>
#include <Engine/Core/Rendering/Primitive/PrimitiveMeshGenerator.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>

#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Rendering/Meshes/Utility/MeshNormalMatrixUtility.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <bit>
#include <cmath>
#include <cstddef>
#include <memory>
#include <span>
#include <unordered_set>
#include <variant>

//============================================================================
//	RaytracingSceneBuilder internal
//============================================================================
namespace Engine::RaytracingSceneGeometryUtility {

	constexpr uint32_t kRaytracingRenderFlagLighting = 1u << 1;
	constexpr uint32_t kRaytracingRenderFlagReceiveShadow = 1u << 2;
	constexpr uint32_t kRaytracingRenderFlagReceiveIBL = 1u << 3;
	constexpr uint32_t kRaytracingRenderFlagReceiveReflection = 1u << 4;

	uint32_t ToRaytracingRenderFlags(Engine::MeshRenderFlags flags) {

		uint32_t result = 0;
		if (Engine::HasMeshRenderFlag(flags,
			Engine::MeshRenderFlags::Lighting)) {

			result |= kRaytracingRenderFlagLighting;
		}
		if (Engine::HasMeshRenderFlag(flags,
			Engine::MeshRenderFlags::ReceiveShadow)) {

			result |= kRaytracingRenderFlagReceiveShadow;
		}
		if (Engine::HasMeshRenderFlag(flags,
			Engine::MeshRenderFlags::ReceiveIBL)) {

			result |= kRaytracingRenderFlagReceiveIBL;
		}
		if (Engine::HasMeshRenderFlag(flags,
			Engine::MeshRenderFlags::ReceiveReflection)) {

			result |= kRaytracingRenderFlagReceiveReflection;
		}
		return result;
	}

	D3D12_RAYTRACING_INSTANCE_FLAGS ToRaytracingCullFlags(
		const D3D12_RASTERIZER_DESC& rasterizer) {

		D3D12_RAYTRACING_INSTANCE_FLAGS flags =
			D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		if (rasterizer.CullMode == D3D12_CULL_MODE_NONE) {

			return D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
		}

		// 反射レイは背面を除外するため、前面カリングは表裏定義を反転して再現する
		bool frontCounterClockwise = rasterizer.FrontCounterClockwise != FALSE;
		if (rasterizer.CullMode == D3D12_CULL_MODE_FRONT) {
			frontCounterClockwise = !frontCounterClockwise;
		}
		if (frontCounterClockwise) {
			flags |= D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE;
		}
		return flags;
	}

	const Engine::PipelineVariantDesc* ResolvePrimitivePipelineVariant(
		Engine::RenderAssetLibrary& assetLibrary,
		const Engine::MaterialAsset& material,
		Engine::MaterialSurfaceMode surfaceMode,
		const Engine::GraphicsRuntimeFeatures& runtimeFeatures) {

		const Engine::MaterialPassKind passKind =
			surfaceMode == Engine::MaterialSurfaceMode::Transparent ?
			Engine::MaterialPassKind::Transparent :
			Engine::MaterialPassKind::Draw;
		const Engine::MaterialPassBinding* pass =
			Engine::FindPass(material, passKind);
		if (!pass) {
			return nullptr;
		}
		const Engine::RenderPipelineAsset* pipeline =
			assetLibrary.LoadPipeline(pass->pipeline);
		return pipeline ?
			Engine::ResolveBestVariant(
				*pipeline, pass->preferredVariant, runtimeFeatures) :
			nullptr;
	}

	uint64_t ComputeGeometryLayoutHash(
		std::span<const Engine::SubMeshMaterial> subMeshes,
		uint32_t geometryCount) {

		uint64_t hash = geometryCount;
		for (uint32_t index = 0; index < geometryCount; ++index) {

			const Engine::Matrix4x4 localMatrix =
				index < subMeshes.size() ?
				Engine::MeshSubMeshRuntime::BuildRenderLocalMatrix(subMeshes[index]) :
				Engine::Matrix4x4::Identity();
			for (uint32_t row = 0; row < 4; ++row) {
				for (uint32_t column = 0; column < 4; ++column) {

					Engine::Algorithm::HashCombine(hash,
						std::bit_cast<uint32_t>(localMatrix.m[row][column]));
				}
			}
		}
		return hash;
	}

	uint64_t ComputeSceneMaterialHash(
		std::span<const Engine::MeshSubMeshShaderData> subMeshes) {

		uint64_t hash = 1469598103934665603ull;
		Engine::Algorithm::HashCombine(hash,
			static_cast<uint64_t>(subMeshes.size()));
		const auto hashFloat = [&hash](float value) {

			Engine::Algorithm::HashCombine(hash,
				std::bit_cast<uint32_t>(value));
		};
		const auto hashColor = [&hashFloat](const Engine::Color4& color) {

			hashFloat(color.r);
			hashFloat(color.g);
			hashFloat(color.b);
			hashFloat(color.a);
		};
		for (const Engine::MeshSubMeshShaderData& subMesh : subMeshes) {

			Engine::Algorithm::HashCombine(hash,
				subMesh.baseColorTextureIndex);
			Engine::Algorithm::HashCombine(hash,
				subMesh.normalTextureIndex);
			Engine::Algorithm::HashCombine(hash,
				subMesh.metallicRoughnessTextureIndex);
			Engine::Algorithm::HashCombine(hash,
				subMesh.emissiveTextureIndex);
			Engine::Algorithm::HashCombine(hash,
				subMesh.occlusionTextureIndex);
			Engine::Algorithm::HashCombine(hash,
				subMesh.specularTextureIndex);
			Engine::Algorithm::HashCombine(hash,
				subMesh.metallicTextureIndex);
			Engine::Algorithm::HashCombine(hash,
				subMesh.roughnessTextureIndex);
			hashFloat(subMesh.metallic);
			hashFloat(subMesh.roughness);
			hashColor(subMesh.importedBaseColor);
			hashColor(subMesh.color);
			hashColor(subMesh.emissiveColor);
			for (uint32_t row = 0; row < 4; ++row) {
				for (uint32_t column = 0; column < 4; ++column) {

					hashFloat(subMesh.uvMatrix.m[row][column]);
				}
			}
		}
		return hash;
	}

	bool RequiresTLASRebuildForTraceQuality(
		size_t instanceCount, uint32_t changedInstanceCount) {

		return 256 <= changedInstanceCount &&
			instanceCount <=
				static_cast<size_t>(changedInstanceCount) * 4;
	}

	float GetMatrixMaxScale(const Engine::Matrix4x4& matrix) {

		const float scaleX = std::sqrt(
			matrix.m[0][0] * matrix.m[0][0] +
			matrix.m[0][1] * matrix.m[0][1] +
			matrix.m[0][2] * matrix.m[0][2]);
		const float scaleY = std::sqrt(
			matrix.m[1][0] * matrix.m[1][0] +
			matrix.m[1][1] * matrix.m[1][1] +
			matrix.m[1][2] * matrix.m[1][2]);
		const float scaleZ = std::sqrt(
			matrix.m[2][0] * matrix.m[2][0] +
			matrix.m[2][1] * matrix.m[2][1] +
			matrix.m[2][2] * matrix.m[2][2]);
		return (std::max)(scaleX, (std::max)(scaleY, scaleZ));
	}

	void EncapsulateSphere(const Engine::Vector3& sourceCenter,
		float sourceRadius, Engine::Vector3& center, float& radius) {

		const Engine::Vector3 difference = sourceCenter - center;
		const float distance = difference.Length();
		if (distance + sourceRadius <= radius) {
			return;
		}
		if (distance + radius <= sourceRadius) {
			center = sourceCenter;
			radius = sourceRadius;
			return;
		}

		const float newRadius =
			(distance + radius + sourceRadius) * 0.5f;
		if (0.00001f < distance) {
			center += difference *
				((newRadius - radius) / distance);
		}
		radius = newRadius;
	}

	void CalculateMeshWorldBounds(
		const Engine::MeshGPUResource& meshResource,
		std::span<const Engine::SubMeshMaterial> subMeshes,
		const Engine::Matrix4x4& worldMatrix,
		Engine::Vector3& outCenter, float& outRadius) {

		outCenter = Engine::Vector3::Transform(
			meshResource.boundsCenter, worldMatrix);
		outRadius = meshResource.boundsRadius *
			GetMatrixMaxScale(worldMatrix);
		if (subMeshes.empty()) {
			return;
		}

		bool initialized = false;
		for (const Engine::SubMeshMaterial& subMesh : subMeshes) {

			const Engine::Matrix4x4 localMatrix =
				Engine::MeshSubMeshRuntime::
					BuildRenderLocalMatrix(subMesh);
			const Engine::Vector3 localCenter =
				Engine::Vector3::Transform(
					meshResource.boundsCenter, localMatrix);
			const float localRadius =
				meshResource.boundsRadius *
				GetMatrixMaxScale(localMatrix);
			const Engine::Vector3 worldCenter =
				Engine::Vector3::Transform(
					localCenter, worldMatrix);
			const float worldRadius =
				localRadius * GetMatrixMaxScale(worldMatrix);
			if (!initialized) {
				outCenter = worldCenter;
				outRadius = worldRadius;
				initialized = true;
				continue;
			}
			EncapsulateSphere(
				worldCenter, worldRadius, outCenter, outRadius);
		}
	}

	const Engine::MeshLODRange& ResolveRaytracingLODRange(
		const Engine::SubMeshDesc& subMesh, uint32_t lodIndex) {

		uint32_t resolvedLOD = (std::min)(
			lodIndex, Engine::kMeshLODCount - 1);
		while (0 < resolvedLOD &&
			subMesh.lods[resolvedLOD].indexCount < 3) {
			--resolvedLOD;
		}
		return subMesh.lods[resolvedLOD];
	}

	uint32_t ResolveMeshLOD(
		const Engine::GraphicsRuntimeFeatures& features,
		const Engine::ResolvedRenderView* cullingView,
		const Engine::Vector3& center, float radius) {

		if (!features.useMeshLOD || !cullingView) {
			return 0;
		}
		const Engine::ResolvedCameraView* camera =
			cullingView->FindCamera(
				Engine::RenderCameraDomain::Perspective);
		if (!camera || !camera->valid) {
			return 0;
		}

		const Engine::Vector3 viewCenter =
			Engine::Vector3::Transform(
				center, camera->matrices.viewMatrix);
		const float nearZ = viewCenter.z - radius;
		if (nearZ <= (std::max)(camera->nearClip, 0.00001f)) {
			return 0;
		}

		const float projectionX = std::abs(
			camera->matrices.projectionMatrix.m[0][0]);
		const float projectionY = std::abs(
			camera->matrices.projectionMatrix.m[1][1]);
		const float pixelRadiusX =
			std::abs(radius * projectionX / nearZ) *
			static_cast<float>((std::max)(cullingView->width, 1u)) *
			0.5f;
		const float pixelRadiusY =
			std::abs(radius * projectionY / nearZ) *
			static_cast<float>((std::max)(cullingView->height, 1u)) *
			0.5f;
		const float pixelRadius =
			(std::max)(pixelRadiusX, pixelRadiusY);
		if (features.meshLOD0PixelThreshold <= pixelRadius) {
			return 0;
		}
		if (features.meshLOD1PixelThreshold <= pixelRadius) {
			return 1;
		}
		if (features.meshLOD2PixelThreshold <= pixelRadius) {
			return 2;
		}
		return Engine::kMeshLODCount - 1;
	}

	uint64_t ComputeLODViewHash(
		const Engine::GraphicsRuntimeFeatures& features,
		const Engine::ResolvedRenderView* cullingView) {

		uint64_t hash = features.useMeshLOD ? 1ull : 0ull;
		Engine::Algorithm::HashCombine(hash,
			std::bit_cast<uint32_t>(
				features.meshLOD0PixelThreshold));
		Engine::Algorithm::HashCombine(hash,
			std::bit_cast<uint32_t>(
				features.meshLOD1PixelThreshold));
		Engine::Algorithm::HashCombine(hash,
			std::bit_cast<uint32_t>(
				features.meshLOD2PixelThreshold));
		if (!cullingView) {
			return hash;
		}

		Engine::Algorithm::HashCombine(hash, cullingView->width);
		Engine::Algorithm::HashCombine(hash, cullingView->height);
		const Engine::ResolvedCameraView* camera =
			cullingView->FindCamera(
				Engine::RenderCameraDomain::Perspective);
		if (!camera || !camera->valid) {
			return hash;
		}
		Engine::Algorithm::HashCombine(hash,
			std::bit_cast<uint32_t>(camera->nearClip));
		for (uint32_t row = 0; row < 4; ++row) {
			for (uint32_t column = 0; column < 4; ++column) {

				Engine::Algorithm::HashCombine(hash,
					std::bit_cast<uint32_t>(
						camera->matrices.viewMatrix.
							m[row][column]));
			}
		}
		Engine::Algorithm::HashCombine(hash,
			std::bit_cast<uint32_t>(
				camera->matrices.projectionMatrix.m[0][0]));
		Engine::Algorithm::HashCombine(hash,
			std::bit_cast<uint32_t>(
				camera->matrices.projectionMatrix.m[1][1]));
		return hash;
	}
}
