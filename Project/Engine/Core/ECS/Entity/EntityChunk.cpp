#include "EntityChunk.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Registry/ComponentTypeRegistry.h>

//============================================================================
//	EntityChunk classMethods
//============================================================================

void Engine::AlignedBuffer::Reset(size_t argBytes, size_t argAlign) {

	Release();
	bytes = argBytes;
	align = argAlign;
	ptr = (std::byte*)::operator new(bytes, std::align_val_t(align));
}

void Engine::AlignedBuffer::Release() {

	// バッファが存在する場合は解放する
	if (ptr) {
		::operator delete(ptr, std::align_val_t(align));
		ptr = nullptr;
	}
	bytes = 0;
	align = 0;
}

Engine::AlignedBuffer& Engine::AlignedBuffer::operator=(AlignedBuffer&& other) noexcept {

	if (this == &other) {
		return *this;
	}
	// 既存のバッファを解放してから、他のバッファの所有権を移動する
	Release();
	ptr = other.ptr;
	bytes = other.bytes;
	align = other.align;
	other.ptr = nullptr;
	other.bytes = 0;
	other.align = 0;
	return *this;
}

Engine::EntityChunk::EntityChunk(const std::vector<uint32_t>& types) {

	// Archetypeが持つコンポーネント一覧を受け取って、列を確保する
	columns_.reserve(types.size());
	for (const auto& type : types) {

		const auto& info = ComponentTypeRegistry::GetInstance().GetInfo(type);
		EntityColumn column{};
		column.typeID = type;
		column.info = &info;
		column.storage.Reset(info.size * kChunkCapacity, info.align);
		columns_.push_back(std::move(column));
	}
	entities_.reserve(kChunkCapacity);
}

Engine::EntityChunk::~EntityChunk() {

	// Chunk破棄時に、まだ生きている全コンポーネントを破棄する
	DestroyAllRows();
}

uint32_t Engine::EntityChunk::AddEntity(const Entity& entity) {

	// まず行だけ確保
	uint32_t row = AddEntityUninitialized(entity);

	// 全列をデフォルト構築
	for (auto& column : columns_) {

		column.info->constructDefault(GetPtr(column, row));
	}
	return row;
}

uint32_t Engine::EntityChunk::AddEntityUninitialized(const Entity& entity) {

	assert(HasSpace());

	uint32_t row = GetCount();
	entities_.emplace_back(entity);
	return row;
}

Engine::Entity Engine::EntityChunk::RemoveSwap(uint32_t row) {

	// 空きがあることを確認
	assert(row < GetCount());

	// 最後のエンティティ番目
	uint32_t last = GetCount() - 1;
	// 移動先
	Entity moved = Entity::Null();

	// 入れ替える行が最後の行かどうか
	if (row != last) {

		// 空の移動先を最後の行から入れ替える
		moved = entities_[last];
		entities_[row] = moved;

		for (auto& column : columns_) {

			void* dst = GetPtr(column, row);
			void* src = GetPtr(column, last);
			column.info->destroy(dst);
			column.info->moveConstruct(dst, src);
			column.info->destroy(src);
		}
	} else {
		// 最後の行を削除するだけでいいので、全列のセルを破棄する
		for (auto& column : columns_) {

			column.info->destroy(GetPtr(column, last));
		}
	}
	// 最後の行を削除
	entities_.pop_back();
	return moved;
}

void Engine::EntityChunk::ConstructDefaultByColumnIndex(uint32_t columnIndex, uint32_t row) {

	assert(columnIndex < columns_.size());
	assert(row < GetCount());

	auto& column = columns_[columnIndex];
	column.info->constructDefault(GetPtr(column, row));
}

void* Engine::EntityChunk::GetRawByColumnIndex(uint32_t columnIndex, uint32_t row) {

	// 列と行が存在することを確認
	assert(columnIndex < columns_.size());
	return GetPtr(columns_[columnIndex], row);
}

void* Engine::EntityChunk::GetPtr(EntityColumn& column, uint32_t row) {

	return (void*)(column.storage.ptr + static_cast<size_t>(row) * column.info->size);
}

void Engine::EntityChunk::DestroyAllRows() {

	// RemoveSwap済みの行はエンティティから外れているので、現在生存している行だけを破棄する
	const uint32_t aliveCount = GetCount();
	for (uint32_t row = 0; row < aliveCount; ++row) {
		for (auto& column : columns_) {
			column.info->destroy(GetPtr(column, row));
		}
	}
	// 生存管理側も空にする
	entities_.clear();
}