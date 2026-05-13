#include "EntityArchetype.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Logger/Assert.h>

//============================================================================
//	EntityArchetype classMethods
//============================================================================

Engine::EntityArchetype::EntityArchetype(const EntitySignature& signature, const std::vector<uint32_t>& types) :
	signature_(signature), types_(types) {

	// Archetypeが持つコンポーネント種類IDから、EntityChunk内の列番号へのマップを作る
	typeToColumn_.fill(kInvalidColumnIndex);
	for (uint32_t i = 0; i < static_cast<uint32_t>(types_.size()); ++i) {

		Assert::Call(types_[i] < kMaxComponentTypes, "types_[i] < kMaxComponentTypes");
		typeToColumn_[types_[i]] = i;
	}
	// 最初のチャンクを作っておく
	chunks_.push_back(std::make_unique<EntityChunk>(types_));
}

std::pair<uint32_t, uint32_t> Engine::EntityArchetype::Add(const Entity& entity) {

	const uint32_t chunkIndex = FindWritableChunkIndex();
	uint32_t row = chunks_[chunkIndex]->AddEntity(entity);
	if (!chunks_[chunkIndex]->HasSpace()) {

		++firstWritableChunkIndex_;
	}
	return { chunkIndex, row };
}

std::pair<uint32_t, uint32_t> Engine::EntityArchetype::AddUninitialized(const Entity& entity) {

	const uint32_t chunkIndex = FindWritableChunkIndex();
	uint32_t row = chunks_[chunkIndex]->AddEntityUninitialized(entity);
	if (!chunks_[chunkIndex]->HasSpace()) {

		++firstWritableChunkIndex_;
	}
	return { chunkIndex, row };
}

Engine::Entity Engine::EntityArchetype::RemoveSwap(uint32_t chunkIndex, uint32_t row) {

	Assert::Call(chunkIndex < GetChunkCount(), "chunkIndex < GetChunkCount()");
	Entity moved = chunks_[chunkIndex]->RemoveSwap(row);
	if (chunkIndex < firstWritableChunkIndex_) {

		firstWritableChunkIndex_ = chunkIndex;
	}
	return moved;
}

void Engine::EntityArchetype::ConstructDefault(uint32_t chunkIndex, uint32_t row, uint32_t typeID) {

	Assert::Call(chunkIndex < GetChunkCount(), "chunkIndex < GetChunkCount()");
	chunks_[chunkIndex]->ConstructDefaultByColumnIndex(GetColumnIndex(typeID), row);
}

bool Engine::EntityArchetype::Has(uint32_t typeID) const {

	return typeID < kMaxComponentTypes && typeToColumn_[typeID] != kInvalidColumnIndex;
}

uint32_t Engine::EntityArchetype::GetColumnIndex(uint32_t typeID) const {

	Assert::Call(Has(typeID), "Has(typeID)");
	return typeToColumn_[typeID];
}

void* Engine::EntityArchetype::GetRaw(int32_t chunkIndex, uint32_t row, uint32_t typeID) {

	return chunks_[chunkIndex]->GetRawByColumnIndex(GetColumnIndex(typeID), row);
}

uint32_t Engine::EntityArchetype::FindWritableChunkIndex() {

	// すでに満杯になったチャンクは次回以降の探索から外す
	for (uint32_t i = firstWritableChunkIndex_; i < GetChunkCount(); ++i) {
		if (chunks_[i]->HasSpace()) {

			firstWritableChunkIndex_ = i;
			return i;
		}
	}

	// 空きがなければ新しいチャンクを追加
	chunks_.push_back(std::make_unique<EntityChunk>(types_));
	firstWritableChunkIndex_ = GetChunkCount() - 1;
	return firstWritableChunkIndex_;
}
