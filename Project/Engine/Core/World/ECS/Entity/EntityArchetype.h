#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/EntitySignature.h>
#include <Engine/Core/World/ECS/Entity/EntityChunk.h>

// c++
#include <limits>

namespace Engine {

	//============================================================================
	//	EntityArchetype class
	//	同じシグネチャのエンティティの集合を表すクラス
	//============================================================================
	class EntityArchetype {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		explicit EntityArchetype(const EntitySignature& signature, const std::vector<uint32_t>& types);
		~EntityArchetype() = default;

		// 新しいエンティティを追加し追加したエンティティのチャンク番号と行番号を返す
		std::pair<uint32_t, uint32_t> Add(const Entity& entity);
		// 行だけ確保して、コンポーネントはまだ構築しない
		std::pair<uint32_t, uint32_t> AddUninitialized(const Entity& entity);
		// row番目のエンティティを削除し最後の行と入れ替えて入れ替えたエンティティを返す
		Entity RemoveSwap(uint32_t chunkIndex, uint32_t row);

		// 指定コンポーネントだけデフォルト構築する
		void ConstructDefault(uint32_t chunkIndex, uint32_t row, uint32_t typeID);

		//--------- accessor -----------------------------------------------------

		// typeIDのコンポーネントを持っているか
		bool Has(uint32_t typeID) const;
		// typeIDの列番号を返す
		uint32_t GetColumnIndex(uint32_t typeID) const;
		// chunkIndex番目のチャンクのrow番目のエンティティのtypeIDのコンポーネントデータへのポインタを返す
		void* GetRaw(int32_t chunkIndex, uint32_t row, uint32_t typeID);

		// Archetypeが持つコンポーネント種類のIDの配列
		const std::vector<uint32_t>& GetTypes() const { return types_; }
		// Archetypeのシグネチャ
		const EntitySignature& GetSignature() const { return signature_; }
		// チャンクの数
		uint32_t GetChunkCount() const { return static_cast<uint32_t>(chunks_.size()); }
		// チャンクの配列
		const std::vector<std::unique_ptr<EntityChunk>>& GetChunks() const { return chunks_; }
		// チャンク配置
		const EntityChunkLayout& GetChunkLayout() const { return chunkLayout_; }
		// 確保済みチャンク数
		uint32_t GetAllocatedChunkCount() const;
		// 確保済みチャンクバイト数
		size_t GetAllocatedBytes() const;
		// 有効データのバイト数
		size_t GetPayloadBytes() const;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// Archetypeのシグネチャ
		EntitySignature signature_{};
		// Archetypeが持つコンポーネント種類のIDの配列
		std::vector<uint32_t> types_;
		// 全チャンクで共有する列配置
		EntityChunkLayout chunkLayout_{};

		static constexpr uint16_t kInvalidColumnIndex = (std::numeric_limits<uint16_t>::max)();

		// Archetypeが持つコンポーネント種類IDから、EntityChunk内の列番号へのテーブル
		std::vector<uint16_t> typeToColumn_;
		// 同じArchetypeのエンティティをまとめて保持するEntityChunkの配列
		std::vector<std::unique_ptr<EntityChunk>> chunks_;
		// 次に空きが見つかりやすいチャンク番号
		uint32_t firstWritableChunkIndex_ = 0;

		// 空きがあるチャンク番号を返し、なければ新しく作る
		uint32_t FindWritableChunkIndex();
	};
} // Engine

