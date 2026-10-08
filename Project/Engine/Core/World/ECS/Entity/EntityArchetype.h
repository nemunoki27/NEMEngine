#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/EntitySignature.h>
#include <Engine/Core/World/ECS/Entity/EntityChunk.h>

// c++
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

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

		explicit EntityArchetype(const EntitySignature& signature, const std::vector<uint32_t>& types,
			std::shared_ptr<const ECSWorldLifetime> lifetime = {});
		~EntityArchetype() = default;
		EntityArchetype(const EntityArchetype&) = delete;
		EntityArchetype& operator=(const EntityArchetype&) = delete;
		EntityArchetype(EntityArchetype&&) = delete;
		EntityArchetype& operator=(EntityArchetype&&) = delete;

		// 新しいエンティティを追加し追加したエンティティのチャンク番号と行番号を返す
		std::pair<uint32_t, uint32_t> Add(const Entity& entity, uint64_t firstInstanceID);
		// 行だけ確保して、コンポーネントはまだ構築しない
		std::pair<uint32_t, uint32_t> AddUninitialized(const Entity& entity);
		// 指定行を削除し、末尾から移したEntityを返す
		Entity RemoveSwap(uint32_t chunkIndex, uint32_t row);

		// 指定コンポーネントだけデフォルト構築する
		void ConstructDefault(uint32_t chunkIndex, uint32_t row, uint32_t typeID, uint64_t instanceID);

		//--------- accessor -----------------------------------------------------

		// 指定型のComponentを持っているか
		bool Has(uint32_t typeID) const;
		// 指定型の列番号を返す
		uint32_t GetColumnIndex(uint32_t typeID) const;
		// 指定行のComponentを取得する
		void* GetRaw(int32_t chunkIndex, uint32_t row, uint32_t typeID);
		const void* GetRaw(int32_t chunkIndex, uint32_t row, uint32_t typeID) const;

		// 所持するComponentの型一覧
		const std::vector<uint32_t>& GetTypes() const { return types_; }
		// 所持する型の組合せ
		const EntitySignature& GetSignature() const { return signature_; }
		// チャンクの数
		uint32_t GetChunkCount() const { return static_cast<uint32_t>(chunks_.size()); }
		// 指定位置のChunkを取得する
		EntityChunk& GetChunk(uint32_t index) { return *chunks_[index]; }
		const EntityChunk& GetChunk(uint32_t index) const { return *chunks_[index]; }
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

		static constexpr uint16_t kInvalidColumnIndex = (std::numeric_limits<uint16_t>::max)();

		// 所持する型の組合せ
		EntitySignature signature_{};
		// 所持するComponentの型一覧
		std::vector<uint32_t> types_;
		// 全チャンクで共有する列配置
		EntityChunkLayout chunkLayout_{};

		// 新しいChunkへ引き継ぐWorldの終了状態
		std::shared_ptr<const ECSWorldLifetime> lifetime_;

		// 型IDから列番号への対応表
		std::vector<uint16_t> typeToColumn_;
		// 同じ構成のEntityを格納するChunk
		std::vector<std::unique_ptr<EntityChunk>> chunks_;
		// 次に空きが見つかりやすいチャンク番号
		uint32_t firstWritableChunkIndex_ = 0;

		//--------- functions ----------------------------------------------------

		// 空きがあるチャンク番号を返し、なければ新しく作る
		uint32_t FindWritableChunkIndex();
	};
} // Engine

