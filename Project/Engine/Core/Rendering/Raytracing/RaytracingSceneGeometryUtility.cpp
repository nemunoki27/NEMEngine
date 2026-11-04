#include "RaytracingSceneGeometryUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/Foundation/Utility/Algorithm/HashUtility.h>

// c++
#include <algorithm>
#include <bit>
#include <cmath>

//============================================================================
//	RaytracingSceneGeometryUtility namespaceMethods
//============================================================================
namespace Engine::RaytracingSceneGeometryUtility {

	void CollectRenderTextureParameters(const MaterialParameterSet& parameters, const AssetDatabase& database,
		std::unordered_set<AssetID>& inputs) {

		// 同じCamera出力の状態変更をまとめる
		for (const auto& parameter : parameters.GetRecords()) {

			const auto* texture = std::get_if<AssetID>(&parameter.namedValue.second.value);
			const auto* meta = texture ? database.Find(*texture) : nullptr;
			if (meta && meta->type == AssetType::RenderTexture) {
				inputs.insert(*texture);
			}
		}
	}

	constexpr uint32_t kRaytracingRenderFlagLighting = 1u << 1;
	constexpr uint32_t kRaytracingRenderFlagReceiveShadow = 1u << 2;
	constexpr uint32_t kRaytracingRenderFlagReceiveIBL = 1u << 3;
	constexpr uint32_t kRaytracingRenderFlagReceiveReflection = 1u << 4;
	constexpr uint32_t kRaytracingRenderingLayerMaskShift = 8u;
	constexpr uint32_t kRaytracingRenderingLayerMaskBits = 0x00FFFFFFu;

	uint32_t ToRaytracingRenderFlags(
		Engine::MeshRenderFlags flags, uint32_t renderingLayerMask) {

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
		// Rendererの照明設定をインスタンスへ格納
		return result | ((renderingLayerMask &
			kRaytracingRenderingLayerMaskBits) <<
			kRaytracingRenderingLayerMaskShift);
	}

	D3D12_RAYTRACING_INSTANCE_FLAGS ToRaytracingCullFlags(
		const D3D12_RASTERIZER_DESC& rasterizer, bool reverseWinding) {

		D3D12_RAYTRACING_INSTANCE_FLAGS flags =
			D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		if (rasterizer.CullMode == D3D12_CULL_MODE_NONE) {

			flags |= D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
		}

		// 生成元が持つ頂点順の違いをDXRの表裏へ反映する
		bool frontCounterClockwise = (rasterizer.FrontCounterClockwise != FALSE) != reverseWinding;
		// 反射レイは背面を除外するため、前面カリングは表裏定義を反転して再現する
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

	uint64_t ComputeGeometryLayoutHash(std::span<const Engine::SubMeshMaterial> subMeshes, uint32_t geometryCount) {

		uint64_t hash = geometryCount;
		for (uint32_t index = 0; index < geometryCount; ++index) {
			Engine::Algorithm::HashCombine(hash,
				index < subMeshes.size() && !subMeshes[index].visible ? 0u : 1u);

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

	uint64_t ComputeSceneMaterialHash(std::span<const Engine::MeshSubMeshShaderData> subMeshes) {

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
				subMesh.opacityTextureIndex);
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

	bool RequiresTLASRebuildForTraceQuality(size_t instanceCount, uint32_t changedInstanceCount) {

		return 256 <= changedInstanceCount &&
			instanceCount <=
				static_cast<size_t>(changedInstanceCount) * 4;
	}

	float GetMatrixMaxScale(const Engine::Matrix4x4& matrix) {

		const Engine::Vector3 x(matrix.m[0][0], matrix.m[0][1], matrix.m[0][2]);
		const Engine::Vector3 y(matrix.m[1][0], matrix.m[1][1], matrix.m[1][2]);
		const Engine::Vector3 z(matrix.m[2][0], matrix.m[2][1], matrix.m[2][2]);
		const float xy = std::abs(Engine::Vector3::Dot(x, y));
		const float xz = std::abs(Engine::Vector3::Dot(x, z));
		const float yz = std::abs(Engine::Vector3::Dot(y, z));
		// Shaderと同じGram行列の上限でせん断後の球も包む
		return std::sqrt((std::max)({ Engine::Vector3::Dot(x, x) + xy + xz,
			Engine::Vector3::Dot(y, y) + xy + yz, Engine::Vector3::Dot(z, z) + xz + yz }));
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

		outCenter = Engine::Vector3::Transform(meshResource.boundsCenter, worldMatrix);
		outRadius = meshResource.boundsRadius *
			GetMatrixMaxScale(worldMatrix);
		if (subMeshes.empty()) {
			return;
		}

		bool initialized = false;
		for (const Engine::SubMeshMaterial& subMesh : subMeshes) {
			if (!subMesh.visible) {
				continue;
			}

			const Engine::Matrix4x4 localMatrix =
				Engine::MeshSubMeshRuntime::
					BuildRenderLocalMatrix(subMesh);
			const Engine::Vector3 localCenter =
				Engine::Vector3::Transform(meshResource.boundsCenter, localMatrix);
			const float localRadius =
				meshResource.boundsRadius *
				GetMatrixMaxScale(localMatrix);
			const Engine::Vector3 worldCenter =
				Engine::Vector3::Transform(localCenter, worldMatrix);
			const float worldRadius =
				localRadius * GetMatrixMaxScale(worldMatrix);
			if (!initialized) {
				outCenter = worldCenter;
				outRadius = worldRadius;
				initialized = true;
				continue;
			}
			EncapsulateSphere(worldCenter, worldRadius, outCenter, outRadius);
		}
	}

}
