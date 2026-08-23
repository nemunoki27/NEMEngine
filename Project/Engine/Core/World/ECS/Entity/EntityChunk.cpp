#include "EntityChunk.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>

// c++
#include <cstring>

namespace {

	// 値を指定アライメントへ切り上げる
	size_t AlignUp(size_t value, size_t alignment) {

		return (value + alignment - 1) & ~(alignment - 1);
	}

	// 指定容量でチャンク配置を構築できるか試す
	bool TryBuildLayout(const std::vector<uint32_t>& types, uint32_t capacity,
		Engine::EntityChunkLayout& out) {

		Engine::ComponentTypeRegistry& registry = Engine::ComponentTypeRegistry::GetInstance();
		size_t alignment = (std::max)(Engine::kChunkAlignment, alignof(Engine::Entity));
		size_t offset = 0;

		out.columns.clear();
		out.columns.reserve(types.size());
		out.entityOffset = AlignUp(offset, alignof(Engine::Entity));
		offset = out.entityOffset + sizeof(Engine::Entity) * capacity;

		// Component列を型ごとの連続領域へ分けてSystem走査時の局所性を保つ
		for (uint32_t typeID : types) {

			const Engine::ComponentTypeInfo& info = registry.GetInfo(typeID);
			alignment = (std::max)(alignment, info.align);
			offset = AlignUp(offset, info.align);
			out.columns.emplace_back(Engine::EntityColumnLayout{
				.typeID = typeID,
				.info = &info,
				.offset = offset,
				});
			offset += info.size * capacity;
			if (Engine::kChunkBytes < offset) {
				return false;
			}
		}

		for (Engine::EntityColumnLayout& column : out.columns) {
			if (!column.info->enableable) {
				continue;
			}
			offset = AlignUp(offset, alignof(uint64_t));
			column.enabledOffset = offset;
			offset += sizeof(uint64_t) * ((capacity + 63u) / 64u);
			if (Engine::kChunkBytes < offset) {
				return false;
			}
		}

		out.bytes = Engine::kChunkBytes;
		out.alignment = alignment;
		out.capacity = capacity;
		return true;
	}
}

//============================================================================
//	AlignedBuffer structMethods
//============================================================================
void Engine::AlignedBuffer::Reset(size_t argBytes, size_t argAlign) {

	Release();
	bytes = argBytes;
	align = argAlign;
	ptr = static_cast<std::byte*>(::operator new(bytes, std::align_val_t(align)));
}

void Engine::AlignedBuffer::Release() {

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
	Release();
	ptr = other.ptr;
	bytes = other.bytes;
	align = other.align;
	other.ptr = nullptr;
	other.bytes = 0;
	other.align = 0;
	return *this;
}

//============================================================================
//	EntityChunkLayout structMethods
//============================================================================
Engine::EntityChunkLayout Engine::EntityChunkLayout::Build(const std::vector<uint32_t>& types) {

	ComponentTypeRegistry& registry = ComponentTypeRegistry::GetInstance();
	size_t bytesPerEntity = sizeof(Entity);
	for (uint32_t typeID : types) {
		bytesPerEntity += registry.GetInfo(typeID).size;
	}

	const size_t estimatedCapacity = kChunkBytes / (std::max)(size_t{ 1 }, bytesPerEntity);
	uint32_t capacity = static_cast<uint32_t>((std::min)(
		estimatedCapacity, static_cast<size_t>(kMaxChunkEntities)));
	capacity = (std::max)(capacity, 1u);

	EntityChunkLayout layout{};
	while (0 < capacity) {
		if (TryBuildLayout(types, capacity, layout)) {
			return layout;
		}
		--capacity;
	}

	Assert::Call(false, "Archetypeの1Entity分のデータがChunkサイズを超えています");
	return {};
}

size_t Engine::EntityChunkLayout::GetPayloadBytes(uint32_t count) const {

	size_t bytesPerEntity = sizeof(Entity);
	for (const EntityColumnLayout& column : columns) {
		bytesPerEntity += column.info->size;
	}
	size_t payloadBytes = bytesPerEntity * count;
	for (const EntityColumnLayout& column : columns) {
		if (column.info->enableable) {
			payloadBytes += sizeof(uint64_t) * ((count + 63u) / 64u);
		}
	}
	return payloadBytes;
}

//============================================================================
//	EntityChunk classMethods
//============================================================================
Engine::EntityChunk::EntityChunk(const EntityChunkLayout* layout) :
	layout_(layout) {

	Assert::Call(layout_ != nullptr, "EntityChunkLayoutが必要です");
	Assert::Call(0 < layout_->capacity, "EntityChunkの容量が0です");
}

Engine::EntityChunk::~EntityChunk() {

	DestroyAllRows();
}

uint32_t Engine::EntityChunk::AddEntity(const Entity& entity) {

	const uint32_t row = AddEntityUninitialized(entity);
	for (uint32_t columnIndex = 0;
		columnIndex < static_cast<uint32_t>(layout_->columns.size()); ++columnIndex) {

		ConstructDefaultByColumnIndex(columnIndex, row);
	}
	return row;
}

uint32_t Engine::EntityChunk::AddEntityUninitialized(const Entity& entity) {

	Assert::Call(HasSpace(), "EntityChunkに空きがありません");
	EnsureStorage();

	const uint32_t row = count_;
	GetEntityData()[row] = entity;
	++count_;
	return row;
}

Engine::Entity Engine::EntityChunk::RemoveSwap(uint32_t row) {

	Assert::Call(row < GetCount(), "EntityChunkの削除行が要素数を超えています");

	const uint32_t last = GetCount() - 1;
	Entity moved = Entity::Null();
	Entity* entities = GetEntityData();

	if (row != last) {

		moved = entities[last];
		entities[row] = moved;

		// 空きを残さず末尾行を移し、Component列の連続性を維持する
		for (const EntityColumnLayout& column : layout_->columns) {

			void* dst = GetPtr(column, row);
			void* src = GetPtr(column, last);
			column.info->destroy(dst);
			column.info->moveConstruct(dst, src);
			column.info->destroy(src);
		}
		for (uint32_t columnIndex = 0;
			columnIndex < static_cast<uint32_t>(layout_->columns.size()); ++columnIndex) {

			if (!layout_->columns[columnIndex].info->enableable) {
				continue;
			}
			SetEnabledByColumnIndex(
				columnIndex, row, IsEnabledByColumnIndex(columnIndex, last));
			SetEnabledByColumnIndex(columnIndex, last, false);
		}
	} else {

		for (uint32_t columnIndex = 0;
			columnIndex < static_cast<uint32_t>(layout_->columns.size()); ++columnIndex) {

			const EntityColumnLayout& column = layout_->columns[columnIndex];
			column.info->destroy(GetPtr(column, last));
			SetEnabledByColumnIndex(columnIndex, last, false);
		}
	}

	--count_;
	ReleaseStorageIfEmpty();
	return moved;
}

void Engine::EntityChunk::ConstructDefaultByColumnIndex(uint32_t columnIndex, uint32_t row) {

	Assert::Call(columnIndex < layout_->columns.size(), "EntityChunkの列番号が範囲外です");
	Assert::Call(row < GetCount(), "EntityChunkの行番号が範囲外です");

	const EntityColumnLayout& column = layout_->columns[columnIndex];
	column.info->constructDefault(GetPtr(column, row));
	SetEnabledByColumnIndex(columnIndex, row, true);
}

void* Engine::EntityChunk::GetRawByColumnIndex(uint32_t columnIndex, uint32_t row) {

	Assert::Call(columnIndex < layout_->columns.size(), "EntityChunkの列番号が範囲外です");
	Assert::Call(row < GetCount(), "EntityChunkの行番号が範囲外です");
	return GetPtr(layout_->columns[columnIndex], row);
}

const void* Engine::EntityChunk::GetRawByColumnIndex(
	uint32_t columnIndex, uint32_t row) const {

	Assert::Call(columnIndex < layout_->columns.size(), "EntityChunkの列番号が範囲外です");
	Assert::Call(row < GetCount(), "EntityChunkの行番号が範囲外です");
	return GetPtr(layout_->columns[columnIndex], row);
}

void* Engine::EntityChunk::GetColumnDataByColumnIndex(uint32_t columnIndex) {

	Assert::Call(columnIndex < layout_->columns.size(), "EntityChunkの列番号が範囲外です");
	if (!storage_.ptr) {
		return nullptr;
	}
	return storage_.ptr + layout_->columns[columnIndex].offset;
}

void Engine::EntityChunk::SetEnabledByColumnIndex(
	uint32_t columnIndex, uint32_t row, bool enabled) {

	Assert::Call(columnIndex < layout_->columns.size(), "EntityChunkの列番号が範囲外です");
	Assert::Call(row < GetCount(), "EntityChunkの行番号が範囲外です");
	const EntityColumnLayout& column = layout_->columns[columnIndex];
	if (!column.info->enableable) {
		return;
	}

	uint64_t* words =
		reinterpret_cast<uint64_t*>(storage_.ptr + column.enabledOffset);
	const uint64_t mask = uint64_t{ 1 } << (row & 63u);
	if (enabled) {
		words[row >> 6u] |= mask;
	} else {
		words[row >> 6u] &= ~mask;
	}
}

bool Engine::EntityChunk::IsEnabledByColumnIndex(
	uint32_t columnIndex, uint32_t row) const {

	Assert::Call(columnIndex < layout_->columns.size(), "EntityChunkの列番号が範囲外です");
	Assert::Call(row < GetCount(), "EntityChunkの行番号が範囲外です");
	const EntityColumnLayout& column = layout_->columns[columnIndex];
	if (!column.info->enableable) {
		return true;
	}

	const uint64_t* words =
		reinterpret_cast<const uint64_t*>(storage_.ptr + column.enabledOffset);
	return (words[row >> 6u] & (uint64_t{ 1 } << (row & 63u))) != 0;
}

std::span<const Engine::Entity> Engine::EntityChunk::GetEntities() const {

	return std::span<const Entity>(GetEntityData(), count_);
}

void* Engine::EntityChunk::GetPtr(const EntityColumnLayout& column, uint32_t row) {

	return storage_.ptr + column.offset + static_cast<size_t>(row) * column.info->size;
}

const void* Engine::EntityChunk::GetPtr(const EntityColumnLayout& column, uint32_t row) const {

	return storage_.ptr + column.offset + static_cast<size_t>(row) * column.info->size;
}

Engine::Entity* Engine::EntityChunk::GetEntityData() {

	return storage_.ptr ?
		reinterpret_cast<Entity*>(storage_.ptr + layout_->entityOffset) : nullptr;
}

const Engine::Entity* Engine::EntityChunk::GetEntityData() const {

	return storage_.ptr ?
		reinterpret_cast<const Entity*>(storage_.ptr + layout_->entityOffset) : nullptr;
}

void Engine::EntityChunk::EnsureStorage() {

	if (!storage_.ptr) {
		// 空Chunkはメモリを持たず、最初のEntity追加時だけ固定長領域を確保する
		storage_.Reset(layout_->bytes, layout_->alignment);
		std::memset(storage_.ptr, 0, storage_.bytes);
	}
}

void Engine::EntityChunk::ReleaseStorageIfEmpty() {

	if (count_ == 0) {
		storage_.Release();
	}
}

void Engine::EntityChunk::DestroyAllRows() {

	if (!storage_.ptr) {
		count_ = 0;
		return;
	}

	for (uint32_t row = 0; row < count_; ++row) {
		for (const EntityColumnLayout& column : layout_->columns) {
			column.info->destroy(GetPtr(column, row));
		}
	}
	count_ = 0;
	storage_.Release();
}
