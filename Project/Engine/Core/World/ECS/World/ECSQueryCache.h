#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/EntityArchetype.h>

// c++
#include <array>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <unordered_map>
#include <vector>

namespace Engine {

	//============================================================================
	//	ECSQueryCache class
	//	アーキタイプの検索計画と列走査を管理する
	//============================================================================
	class ECSQueryCache {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// アーキタイプの世代に合わせて検索計画を取得する
		const std::vector<EntityArchetype*>& Resolve(const EntitySignature& required,
			const std::unordered_map<EntitySignature, std::unique_ptr<EntityArchetype>, EntitySignatureHash>& archetypes,
			uint32_t archetypeVersion);

		// 型IDから列番号を一度だけ解決する
		template <size_t N, size_t... I>
		static std::array<uint32_t, N> ResolveColumnIndices(
			const EntityArchetype& archetype, const std::array<uint32_t, N>& typeIDs, std::index_sequence<I...>);
		// 列先頭を再利用して有効な行を走査する
		template <typename... T, typename Chunk, typename Fn, typename Query, size_t... I>
		static void ForEachChunkFast(Chunk& chunk, const std::array<uint32_t, sizeof...(T)>& columnIndices, Fn& fn,
			const Query& query, std::index_sequence<I...>);

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// 構造世代ごとのアーキタイプ検索結果
		struct MatchPlan {

			// 条件に一致したArchetypeの借用一覧
			std::vector<EntityArchetype*> archetypes;
			// 検索計画を構築したWorldの構造世代
			uint32_t builtArchetypeVersion = 0xFFFFFFFFu;
		};

		//--------- variables ----------------------------------------------------

		// シグネチャごとの検索結果
		std::unordered_map<EntitySignature, MatchPlan, EntitySignatureHash> plans_;
	};

	//============================================================================
	//	ECSQueryCache templateMethods
	//============================================================================
	template <size_t N, size_t... I>
	inline std::array<uint32_t, N> ECSQueryCache::ResolveColumnIndices(
		const EntityArchetype& archetype, const std::array<uint32_t, N>& typeIDs, std::index_sequence<I...>) {

		return {archetype.GetColumnIndex(typeIDs[I])...};
	}

	template <typename... T, typename Chunk, typename Fn, typename Query, size_t... I>
	inline void ECSQueryCache::ForEachChunkFast(Chunk& chunk, const std::array<uint32_t, sizeof...(T)>& columnIndices, Fn& fn,
		const Query& query, std::index_sequence<I...>) {

		const auto entities = chunk.GetEntities();
		const uint32_t count = chunk.GetCount();
		const std::tuple<std::conditional_t<std::is_const_v<Chunk>, const T*, T*>...> columns{
			reinterpret_cast<std::conditional_t<std::is_const_v<Chunk>, const T*, T*>>(
				chunk.GetRawByColumnIndex(columnIndices[I], 0))...};
		for (uint32_t row = 0; row < count; ++row) {

			if (!(chunk.IsEnabledByColumnIndex(columnIndices[I], row) && ...)) {
				continue;
			}
			// チャンクごとに一度解決した列先頭から行を取り出す
			fn(entities[row], (std::get<I>(columns)[row])...);
			// 呼出し中に終了したWorldへ戻らない
			if (!query.IsWorldAlive()) {
				throw std::runtime_error("走査中にWorldが終了しました");
			}
		}
	}

}
