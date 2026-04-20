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
	for (uint32_t i = 0; i < static_cast<uint32_t>(types_.size()); ++i) {

		typeToColumn_[types_[i]] = i;
	}
	// 最初のチャンクを作っておく
	chunks_.push_back(std::make_unique<EntityChunk>(types_));
}

std::pair<uint32_t, uint32_t> Engine::EntityArchetype::Add(const Entity& entity) {

	for (uint32_t i = 0; i < GetChunkCount(); ++i) {
		// 空きがあるチャンクを探す
		if (chunks_[i]->HasSpace()) {

			uint32_t row = chunks_[i]->AddEntity(entity);
			return { i, row };
		}
	}
	// 空きがなければ新しいチャンクを追加
	chunks_.push_back(std::make_unique<EntityChunk>(types_));
	uint32_t chunkIndex = GetChunkCount() - 1;
	uint32_t row = chunks_[chunkIndex]->AddEntity(entity);
	return { chunkIndex, row };
}

std::pair<uint32_t, uint32_t> Engine::EntityArchetype::AddUninitialized(const Entity& entity) {

	for (uint32_t i = 0; i < GetChunkCount(); ++i) {
		if (chunks_[i]->HasSpace()) {
			uint32_t row = chunks_[i]->AddEntityUninitialized(entity);
			return { i, row };
		}
	}
	// 空きがなければ新しいチャンクを追加
	chunks_.push_back(std::make_unique<EntityChunk>(types_));
	uint32_t chunkIndex = GetChunkCount() - 1;
	uint32_t row = chunks_[chunkIndex]->AddEntityUninitialized(entity);
	return { chunkIndex, row };
}

Engine::Entity Engine::EntityArchetype::RemoveSwap(uint32_t chunkIndex, uint32_t row) {

	Assert::Call(chunkIndex < GetChunkCount(), "chunkIndex < GetChunkCount()");
	return chunks_[chunkIndex]->RemoveSwap(row);
}

void Engine::EntityArchetype::ConstructDefault(uint32_t chunkIndex, uint32_t row, uint32_t typeID) {

	Assert::Call(chunkIndex < GetChunkCount(), "chunkIndex < GetChunkCount()");
	chunks_[chunkIndex]->ConstructDefaultByColumnIndex(GetColumnIndex(typeID), row);
}

uint32_t Engine::EntityArchetype::GetColumnIndex(uint32_t typeID) const {

	auto it = typeToColumn_.find(typeID);
	Assert::Call(it != typeToColumn_.end(), "it != typeToColumn_.end()");
	return it->second;
}

void* Engine::EntityArchetype::GetRaw(int32_t chunkIndex, uint32_t row, uint32_t typeID) {

	return chunks_[chunkIndex]->GetRawByColumnIndex(GetColumnIndex(typeID), row);
}