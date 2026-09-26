#include "EntityChunk.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>

// c++
#include <cstring>
#include <stdexcept>

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

uint32_t Engine::EntityChunk::AddEntity(const Entity& entity, uint64_t firstInstanceID) {

	const uint32_t row = AddEntityUninitialized(entity);
	try {
		for (uint32_t columnIndex = 0; columnIndex < layout_->columns.size(); ++columnIndex) {
			ConstructDefaultByColumnIndex(columnIndex, row, firstInstanceID + columnIndex);
		}
	} catch (...) {

		// 構築に成功したセルだけを巻き戻す
		RemoveSwap(row);
		throw;
	}
	return row;
}

uint32_t Engine::EntityChunk::AddEntityUninitialized(const Entity& entity) {

	Assert::Call(HasSpace(), "EntityChunkに空きがありません");
	EnsureStorage();

	const uint32_t row = count_;
	GetEntityData()[row] = entity;
	++count_;
	for (uint32_t columnIndex = 0; columnIndex < layout_->columns.size(); ++columnIndex) {
		SetComponentInstanceID(columnIndex, row, 0);
		SetEnabledByColumnIndex(columnIndex, row, false);
	}
	return row;
}

Engine::Entity Engine::EntityChunk::RemoveSwap(uint32_t row) {

	Assert::Call(row < GetCount(), "EntityChunkの削除行が要素数を超えています");

	const uint32_t last = GetCount() - 1;
	Entity moved = Entity::Null();
	Entity* entities = GetEntityData();

	for (uint32_t columnIndex = 0; columnIndex < layout_->columns.size(); ++columnIndex) {

		DestroyCell(columnIndex, row);
		if (row == last) {
			continue;
		}
		const uint64_t instanceID = GetComponentInstanceID(columnIndex, last);
		if (instanceID != 0) {

			// 末尾の値と個体番号を同じ行へ移す
			const bool enabled = IsEnabledByColumnIndex(columnIndex, last);
			MoveConstructByColumnIndex(columnIndex, row, GetPtr(layout_->columns[columnIndex], last), instanceID);
			SetEnabledByColumnIndex(columnIndex, row, enabled);
			DestroyCell(columnIndex, last);
		}
	}
	if (row != last) {
		moved = entities[last];
		entities[row] = moved;
	}

	--count_;
	ReleaseStorageIfEmpty();
	return moved;
}

void Engine::EntityChunk::ConstructDefaultByColumnIndex(uint32_t columnIndex, uint32_t row, uint64_t instanceID) {

	Assert::Call(instanceID != 0 && GetComponentInstanceID(columnIndex, row) == 0, "Componentの構築状態が不正です");
	const EntityColumnLayout& column = layout_->columns[columnIndex];
	column.info->constructDefault(GetPtr(column, row));
	SetComponentInstanceID(columnIndex, row, instanceID);
	SetEnabledByColumnIndex(columnIndex, row, true);
}

void Engine::EntityChunk::CopyConstructByColumnIndex(uint32_t columnIndex, uint32_t row,
	const void* source, uint64_t instanceID) {

	Assert::Call(instanceID != 0 && GetComponentInstanceID(columnIndex, row) == 0, "Componentの複製先が不正です");
	const EntityColumnLayout& column = layout_->columns[columnIndex];
	column.info->copyConstruct(GetPtr(column, row), source);
	SetComponentInstanceID(columnIndex, row, instanceID);
	SetEnabledByColumnIndex(columnIndex, row, true);
}

void Engine::EntityChunk::MoveConstructByColumnIndex(uint32_t columnIndex, uint32_t row,
	void* source, uint64_t instanceID) {

	Assert::Call(instanceID != 0 && GetComponentInstanceID(columnIndex, row) == 0, "Componentの移動先が不正です");
	const EntityColumnLayout& column = layout_->columns[columnIndex];
	column.info->moveConstruct(GetPtr(column, row), source);
	SetComponentInstanceID(columnIndex, row, instanceID);
	SetEnabledByColumnIndex(columnIndex, row, true);
}

void Engine::EntityChunk::DestroyCell(uint32_t columnIndex, uint32_t row) {

	if (GetComponentInstanceID(columnIndex, row) == 0) {
		return;
	}

	// 破棄対象から外してから値を解放する
	SetComponentInstanceID(columnIndex, row, 0);
	SetEnabledByColumnIndex(columnIndex, row, false);
	const EntityColumnLayout& column = layout_->columns[columnIndex];
	column.info->destroy(GetPtr(column, row));
}

uint64_t Engine::EntityChunk::GetComponentInstanceID(uint32_t columnIndex, uint32_t row) const {

	Assert::Call(columnIndex < layout_->columns.size() && row < count_, "Component個体番号の参照位置が不正です");
	const auto* instances = reinterpret_cast<const uint64_t*>(storage_.ptr + layout_->columns[columnIndex].instanceOffset);
	return instances[row];
}

void Engine::EntityChunk::SetComponentInstanceID(uint32_t columnIndex, uint32_t row, uint64_t instanceID) {

	Assert::Call(columnIndex < layout_->columns.size() && row < count_, "Component個体番号の設定位置が不正です");
	auto* instances = reinterpret_cast<uint64_t*>(storage_.ptr + layout_->columns[columnIndex].instanceOffset);
	instances[row] = instanceID;
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
		for (uint32_t columnIndex = 0; columnIndex < layout_->columns.size(); ++columnIndex) {
			DestroyCell(columnIndex, row);
		}
	}
	count_ = 0;
	storage_.Release();
}
