#include "MeshRenderItemExtractor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

//============================================================================
//	MeshRenderItemExtractor internal
//============================================================================
namespace {

	constexpr uint32_t kSparseGroupMaxCount = 8;
	constexpr uint32_t kSparseGroupOptimizationMaxGroups = 4;

}

//============================================================================
//	MeshRenderItemExtractor classMethods
//============================================================================
void Engine::MeshRenderItemExtractor::Extract(ECSWorld& world, RenderSceneBatch& batch) {

	world.ForEach<MeshRendererComponent>([&](const Entity& entity, MeshRendererComponent& renderer) {

		// 描画可能か
		if (!RenderItemExtract::IsVisible(world, entity, renderer.visible)) {
			return;
		}

		const Matrix4x4 entityWorldMatrix = RenderItemExtract::GetWorldMatrix(world, entity);

		const std::span<const SubMeshMaterial> subMeshes =
			GetMeshSubMeshes(world, entity);
		auto addItem = [&](uint32_t subMeshIndex,
			uint32_t subMeshGroupIndex,
			const SubMeshMaterial* subMesh,
			const MeshSubMeshRenderState* renderState) {

			MeshRenderPayload payload{};
			payload.mesh = renderer.mesh;
			payload.subMeshIndex = subMeshIndex;
			payload.subMeshGroupIndex = subMeshGroupIndex;
			payload.enableZPrepass = renderer.enableZPrepass;

			RenderItem item{};
			RenderItemExtract::FillCommonFields(
				item, world, entity, renderer, entityWorldMatrix);
			item.backendID = RenderBackendID::Mesh;
			item.material = renderState ?
				renderState->material : renderer.material;
			item.castShadows = HasMeshRenderFlag(
				renderer.renderFlags, MeshRenderFlags::CastShadow);
			item.receiveShadows = HasMeshRenderFlag(
				renderer.renderFlags, MeshRenderFlags::ReceiveShadow);
			item.cameraDomain = RenderCameraDomain::Perspective;

			if (renderState) {
				item.surfaceModeOverridden = renderState->surfaceModeOverridden;
				item.surfaceMode = renderState->surfaceMode;
			}
			if (subMesh) {
				const Matrix4x4 subMeshWorld =
					MeshSubMeshRuntime::BuildRenderLocalMatrix(*subMesh) *
					entityWorldMatrix;
				item.sortPosition = Vector3::Transform(
					subMesh->sourcePivot, subMeshWorld);
			}
			item.renderPhase = ResolveMaterialRenderPhase(
				item.surfaceMode, item.renderPhase);
			item.blendMode = ResolveMaterialBlendMode(
				item.surfaceMode, item.blendMode);

			uint64_t batchKey = std::hash<AssetID>{}(renderer.mesh);
			Algorithm::HashCombine(batchKey,
				static_cast<uint64_t>(subMeshIndex));
			Algorithm::HashCombine(batchKey,
				static_cast<uint64_t>(subMeshGroupIndex));
			item.batchKey = batchKey;
			item.payload = batch.PushPayload(payload);
			batch.Add(std::move(item));
			};

		// Material Slotごとに別の描画パスへ振り分ける
		if (subMeshes.empty()) {
			addItem(kAllMeshSubMeshes, UINT32_MAX, nullptr, nullptr);
		} else {
			std::vector<MeshSubMeshRenderState> groups;
			std::vector<uint32_t> groupIndices;
			MeshDrawPathCommon::BuildSubMeshRenderGroups(
				renderer, subMeshes, groups, groupIndices);

			// 同じ描画状態のモデルは従来の一括バッチを使いスキニングと転送の重複を避ける
			if (groups.size() == 1) {
				addItem(kAllMeshSubMeshes, UINT32_MAX, nullptr, &groups.front());
			} else {
				std::vector<uint32_t> groupCounts(groups.size(), 0);
				for (const uint32_t groupIndex : groupIndices) {
					++groupCounts[groupIndex];
				}

				for (uint32_t groupIndex = 0;
					groupIndex < static_cast<uint32_t>(groups.size());
					++groupIndex) {

					// 少数側だけ実範囲で描画して全Meshletをグループ数分走査しない
					const bool useExactSubMeshes =
						groups.size() <= kSparseGroupOptimizationMaxGroups &&
						groupCounts[groupIndex] <= kSparseGroupMaxCount &&
						groupCounts[groupIndex] * 4 <= subMeshes.size();
					if (!useExactSubMeshes) {
						addItem(kAllMeshSubMeshes, groupIndex,
							nullptr, &groups[groupIndex]);
						continue;
					}

					for (uint32_t subMeshIndex = 0;
						subMeshIndex < static_cast<uint32_t>(subMeshes.size());
						++subMeshIndex) {

						if (groupIndices[subMeshIndex] != groupIndex) {
							continue;
						}
						addItem(subMeshIndex, UINT32_MAX,
							&subMeshes[subMeshIndex], &groups[groupIndex]);
					}
				}
			}
		}
		});
}
