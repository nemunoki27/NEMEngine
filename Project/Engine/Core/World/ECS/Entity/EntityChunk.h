#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Core/ComponentType.h>
#include <Engine/Core/Foundation/Utility/AlignedBuffer.h>
#include <Engine/Core/World/ECS/Config/ECSConfig.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>

// c++
#include <vector>
#include <memory>
#include <cstddef>
#include <cstdint>
#include <cassert>
#include <algorithm>
#include <span>
#include <limits>

namespace Engine {

	//============================================================================
	//	EntityColumnLayout struct
	//	チャンク内のコンポーネント列配置
	//============================================================================
	struct EntityColumnLayout {

		// コンポーネントの種類ID
		uint32_t typeID = 0;
		// コンポーネントの種類情報へのポインタ
		const ComponentTypeInfo* info = nullptr;
		// チャンク先頭から列先頭までのオフセット
		size_t offset = 0;
		// Component個体番号の列先頭
		size_t instanceOffset = 0;
		// 有効状態ビット列の先頭、無効なら最大値
		size_t enabledOffset = (std::numeric_limits<size_t>::max)();
	};

	//============================================================================
	//	EntityChunkLayout struct
	//	アーキタイプごとに共有するチャンク配置
	//============================================================================
	struct EntityChunkLayout {

		// エンティティ列の先頭オフセット
		size_t entityOffset = 0;
		// チャンクの確保サイズとアライメント
		size_t bytes = 0;
		size_t alignment = 0;
		// チャンクへ格納できるエンティティ数
		uint32_t capacity = 0;
		// コンポーネント列の配置
		std::vector<EntityColumnLayout> columns;

		// コンポーネント種類から固定バイトチャンクの配置を作る
		static EntityChunkLayout Build(const std::vector<uint32_t>& types);
		// 指定件数分の有効データサイズを返す
		size_t GetPayloadBytes(uint32_t count) const;
	};

	//============================================================================
	//	EntityChunk class
	//	同じアーキタイプのエンティティとコンポーネント列をまとめて保持する
	//============================================================================
	class EntityChunk {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		explicit EntityChunk(const EntityChunkLayout* layout);
		~EntityChunk();

		// 新しいエンティティを追加する
		uint32_t AddEntity(const Entity& entity, uint64_t firstInstanceID);
		// 行だけ確保して、コンポーネントはまだ構築しない
		uint32_t AddEntityUninitialized(const Entity& entity);
		// 指定行を削除し最後の行と入れ替えたエンティティを返す
		Entity RemoveSwap(uint32_t row);

		// 指定列だけデフォルト構築する
		void ConstructDefaultByColumnIndex(uint32_t columnIndex, uint32_t row, uint64_t instanceID);
		// 指定列を複製し構築済みとして公開する
		void CopyConstructByColumnIndex(uint32_t columnIndex, uint32_t row, const void* source, uint64_t instanceID);
		// 指定列へ所有権と個体番号を移す
		void MoveConstructByColumnIndex(uint32_t columnIndex, uint32_t row, void* source, uint64_t instanceID);

		//--------- accessor -----------------------------------------------------

		// 空きがあるか
		bool HasSpace() const { return GetCount() < GetCapacity(); }
		// 指定行列のセルへのポインタを返す
		void* GetRawByColumnIndex(uint32_t columnIndex, uint32_t row);
		const void* GetRawByColumnIndex(
			uint32_t columnIndex, uint32_t row) const;
		// 指定列の先頭ポインタを返す
		void* GetColumnDataByColumnIndex(uint32_t columnIndex);
		// 指定行列の有効状態を設定する
		void SetEnabledByColumnIndex(uint32_t columnIndex, uint32_t row, bool enabled);
		// 指定行列が有効か
		bool IsEnabledByColumnIndex(uint32_t columnIndex, uint32_t row) const;

		// 指定列のComponent個体番号を返す
		uint64_t GetComponentInstanceID(uint32_t columnIndex, uint32_t row) const;

		// 所持しているエンティティ数
		uint32_t GetCount() const { return count_; }
		// 格納できるエンティティ数
		uint32_t GetCapacity() const { return layout_->capacity; }
		// 所持しているエンティティ
		std::span<const Entity> GetEntities() const;
		// メモリ統計用の確保状態
		bool IsAllocated() const { return storage_.ptr != nullptr; }
		size_t GetAllocatedBytes() const { return IsAllocated() ? layout_->bytes : 0; }
		size_t GetPayloadBytes() const { return layout_->GetPayloadBytes(count_); }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// アーキタイプが所有する共有配置
		const EntityChunkLayout* layout_ = nullptr;
		// エンティティ列とコンポーネント列をまとめて保持する単一バッファ
		AlignedBuffer storage_;
		// 同じアーキタイプのエンティティ数
		uint32_t count_ = 0;

		//--------- functions ----------------------------------------------------

		// 構築済みのセルだけを破棄する
		void DestroyCell(uint32_t columnIndex, uint32_t row);
		// Component個体番号を設定する
		void SetComponentInstanceID(uint32_t columnIndex, uint32_t row, uint64_t instanceID);
		// 指定行列のセルへのポインタを返す
		void* GetPtr(const EntityColumnLayout& column, uint32_t row);
		const void* GetPtr(const EntityColumnLayout& column, uint32_t row) const;
		// エンティティ列の先頭ポインタを返す
		Entity* GetEntityData();
		const Entity* GetEntityData() const;
		// 初回追加時にチャンクを確保する
		void EnsureStorage();
		// 空チャンクのメモリを解放する
		void ReleaseStorageIfEmpty();
		// 現在生存している全行のコンポーネントを破棄する
		void DestroyAllRows();
	};
} // Engine
