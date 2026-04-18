#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Type/ComponentType.h>
#include <Engine/Core/ECS/Config/ECSConfig.h>
#include <Engine/Core/ECS/Entity/Entity.h>

// c++
#include <vector>
#include <memory>
#include <cstddef>
#include <cstdint>
#include <cassert>
#include <algorithm>

namespace Engine {

	//============================================================================
	//	AlignedBuffer struct
	//	指定されたバイト数、アライメントのバッファを管理
	//============================================================================
	struct AlignedBuffer {

		AlignedBuffer() = default;
		AlignedBuffer(size_t argBytes, size_t argAlign) { Reset(argBytes, argAlign); }
		~AlignedBuffer() { Release(); }

		// バッファへのポインタ
		std::byte* ptr = nullptr;
		// バイト数、アライメント
		size_t bytes = 0;
		size_t align = 0;

		// バッファを指定されたバイト数、アライメントで再確保する
		void Reset(size_t argBytes, size_t argAlign);
		// バッファを解放して、状態をリセットする
		void Release();

		// コピー禁止
		AlignedBuffer(const AlignedBuffer&) = delete;
		AlignedBuffer& operator=(const AlignedBuffer&) = delete;
		// ムーブ許可
		AlignedBuffer(AlignedBuffer&& other) noexcept { *this = std::move(other); }
		AlignedBuffer& operator=(AlignedBuffer&& other) noexcept;
	};

	//============================================================================
	//	EntityColumn struct
	//	同じ種類のコンポーネントをまとめて保持する列
	//============================================================================
	struct EntityColumn {

		// コンポーネントの種類ID
		uint32_t typeID = 0;
		// コンポーネントの種類情報へのポインタ
		const ComponentTypeInfo* info = nullptr;
		// コンポーネントデータをまとめて保持するバッファ
		AlignedBuffer storage;
	};

	//============================================================================
	//	EntityChunk class
	//	エンティティの塊。Archetypeが持つコンポーネント種類の列を持ち、同じArchetypeのエンティティをまとめて保持する
	//============================================================================
	class EntityChunk {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		EntityChunk(const std::vector<uint32_t>& types);
		~EntityChunk();

		// 新しいエンティティを追加する
		uint32_t AddEntity(const Entity& entity);
		// 行だけ確保して、コンポーネントはまだ構築しない
		uint32_t AddEntityUninitialized(const Entity& entity);
		// row番目のエンティティを削除する。最後の行と入れ替える。入れ替えたエンティティを返す
		Entity RemoveSwap(uint32_t row);

		// 指定列だけデフォルト構築する
		void ConstructDefaultByColumnIndex(uint32_t columnIndex, uint32_t row);

		//--------- accessor -----------------------------------------------------

		// 空きがあるか
		bool HasSpace() const { return GetCount() < kChunkCapacity; }
		// row番目の行のcolumn列のセルへのポインタを返す
		void* GetRawByColumnIndex(uint32_t columnIndex, uint32_t row);

		// 所持しているエンティティ数
		uint32_t GetCount() const { return static_cast<uint32_t>(entities_.size()); }
		// 所持しているエンティティ
		const std::vector<Entity>& GetEntities() const { return entities_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 列の配列。列はArchetypeが持つコンポーネント種類の順で並ぶ
		std::vector<EntityColumn> columns_;
		// 同じArchetypeのエンティティをまとめて保持する配列
		std::vector<Entity> entities_;

		//--------- functions ----------------------------------------------------

		// row番目の行のcolumn列のセルへのポインタを返す
		static void* GetPtr(EntityColumn& column, uint32_t row);
		// 現在生存している全行のコンポーネントを破棄する
		void DestroyAllRows();
	};
} // Engine