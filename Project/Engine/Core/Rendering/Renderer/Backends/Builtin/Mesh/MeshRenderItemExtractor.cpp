#include "MeshRenderItemExtractor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/InvertedHullOutlineComponent.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>
#include <variant>

//============================================================================
//	MeshRenderItemExtractor internal
//============================================================================
namespace {

	using Engine::Algorithm::HashCombine;

	void MixBytes(uint64_t& hash, const void* data, size_t size) {

		// 1バイトずつだと行列(64byte)などで乗算回数が嵩むため、8バイト単位でまとめて混ぜる
		const uint8_t* bytes = static_cast<const uint8_t*>(data);
		size_t offset = 0;
		for (; offset + sizeof(uint64_t) <= size; offset += sizeof(uint64_t)) {

			uint64_t chunk;
			std::memcpy(&chunk, bytes + offset, sizeof(uint64_t));
			HashCombine(hash, chunk);
		}
		// 8バイトに満たない端数はバイト単位で混ぜる
		for (; offset < size; ++offset) {
			HashCombine(hash, bytes[offset]);
		}
	}

	// アウトラインコンポーネントのauthoring値をハッシュへ混ぜる
	// 通常描画とアウトライン描画でリソースを共有するため、編集が即時反映されるようにする
	void MixOutlineComponentHash(uint64_t& h, const Engine::InvertedHullOutlineComponent* outline) {

		HashCombine(h, outline ? 1ull : 0ull);
		if (!outline) {
			return;
		}
		HashCombine(h, outline->enabled ? 1ull : 0ull);
		MixBytes(h, &outline->width, sizeof(outline->width));
		MixBytes(h, &outline->color, sizeof(outline->color));
		HashCombine(h, static_cast<uint64_t>(outline->expansionMode));
		HashCombine(h, static_cast<uint64_t>(outline->widthMode));
		MixBytes(h, &outline->cameraZOffset, sizeof(outline->cameraZOffset));
		HashCombine(h, outline->useBakedNormal ? 1ull : 0ull);
		HashCombine(h, static_cast<uint64_t>(std::hash<Engine::AssetID>{}(outline->bakedNormalTexture)));
		HashCombine(h, outline->useOutlineSampler ? 1ull : 0ull);
		HashCombine(h, static_cast<uint64_t>(std::hash<Engine::AssetID>{}(outline->outlineSamplerTexture)));
		HashCombine(h, outline->useStencil ? 1ull : 0ull);
	}

	// reflection paramの上書きマップを順序非依存で内容ハッシュへ混ぜる
	void MixSubMeshParameterHash(uint64_t& h,
		const std::unordered_map<std::string, Engine::MaterialParameterValue>& parameters) {

		HashCombine(h, static_cast<uint64_t>(parameters.size()));
		uint64_t combined = 0;
		for (const auto& [name, value] : parameters) {

			uint64_t entry = std::hash<std::string>{}(name);
			std::visit([&](const auto& v) {
				MixBytes(entry, &v, sizeof(v));
				}, value.value);
			// XOR集約で要素順に依存しないハッシュにする
			combined ^= entry;
		}
		HashCombine(h, combined);
	}

	// 静的バッチキャッシュキー用の、1アイテム分の内容ハッシュを抽出時に1度だけ計算する
	// backendが毎パス再計算していたbyte走査とcomponent再取得をここへ集約する
	uint64_t ComputeMeshContentHash(const Engine::Entity& entity, Engine::AssetID material,
		Engine::BlendMode blendMode, const Engine::Matrix4x4& worldMatrix,
		const Engine::MeshRendererComponent& renderer, const Engine::InvertedHullOutlineComponent* outline) {

		uint64_t h = 1469598103934665603ull;
		HashCombine(h, entity.index);
		HashCombine(h, entity.generation);
		HashCombine(h, static_cast<uint64_t>(std::hash<Engine::AssetID>{}(material)));
		HashCombine(h, static_cast<uint64_t>(blendMode));
		// Transformが変わるとInstanceDataが変わる
		MixBytes(h, &worldMatrix, sizeof(worldMatrix));
		// アウトライン設定が変わるとGPUデータが変わる
		MixOutlineComponentHash(h, outline);

		HashCombine(h, static_cast<uint64_t>(renderer.subMeshes.size()));
		for (const Engine::SubMeshMaterial& subMesh : renderer.subMeshes) {

			// サブメッシュ編集情報もGPUへ渡すため、内容ハッシュへ含める
			MixBytes(h, &subMesh.stableID, sizeof(subMesh.stableID));
			HashCombine(h, subMesh.sourceSubMeshIndex);
			// reflection paramの上書きが変わるとパラメータバッファが変わる
			MixSubMeshParameterHash(h, subMesh.parameterOverrides);
			MixBytes(h, &subMesh.uvMatrix, sizeof(subMesh.uvMatrix));
			MixBytes(h, &subMesh.localPos, sizeof(subMesh.localPos));
			MixBytes(h, &subMesh.localRotation, sizeof(subMesh.localRotation));
			MixBytes(h, &subMesh.localScale, sizeof(subMesh.localScale));
			MixBytes(h, &subMesh.sourcePivot, sizeof(subMesh.sourcePivot));
		}
		return h;
	}
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

		// 行列の更新
		const Matrix4x4 entityWorldMatrix = RenderItemExtract::GetWorldMatrix(world, entity);
		MeshSubMeshRuntime::UpdateRendererRuntime(renderer, entityWorldMatrix);

		// ペイロード構築
		MeshRenderPayload payload{};
		payload.mesh = renderer.mesh;
		payload.enableZPrepass = renderer.enableZPrepass;
		// 描画アイテムの構築
		RenderItem item{};
		RenderItemExtract::FillCommonFields(item, world, entity, renderer, entityWorldMatrix);
		item.backendID = RenderBackendID::Mesh;
		item.material = renderer.material;
		item.cameraDomain = RenderCameraDomain::Perspective;
		item.batchKey = renderer.mesh.value;
		// 静的バッチキャッシュキー用の内容ハッシュを抽出時に1度だけ計算する(backendの毎パス再ハッシュを避ける)
		const InvertedHullOutlineComponent* outline = world.TryGetComponent<InvertedHullOutlineComponent>(entity);
		item.contentHash = ComputeMeshContentHash(entity, item.material, item.blendMode, item.worldMatrix, renderer, outline);
		item.payload = batch.PushPayload(payload);
		// 描画アイテムをバッチに追加
		batch.Add(std::move(item));
		});
}