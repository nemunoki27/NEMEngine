#include "EntityArchetype.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>

//============================================================================
//	EntityArchetype classMethods
//============================================================================
Engine::EntityArchetype::EntityArchetype(const EntitySignature& signature, const std::vector<uint32_t>& types) :
	signature_(signature), types_(types), chunkLayout_(EntityChunkLayout::Build(types)) {

	// Archetypeが持つコンポーネント種類IDから、EntityChunk内の列番号へのマップを作る
	typeToColumn_.assign(ComponentTypeRegistry::GetInstance().GetComponentTypeCount(), kInvalidColumnIndex);
	for (uint32_t i = 0; i < static_cast<uint32_t>(types_.size()); ++i) {

		Assert::Call(types_[i] < kMaxComponentTypes, "ComponentType IDが上限を超えています");
		Assert::Call(i < kInvalidColumnIndex, "チャンク列数がuint16_tの範囲を超えています");
		typeToColumn_[types_[i]] = static_cast<uint16_t>(i);
	}
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

	Assert::Call(chunkIndex < GetChunkCount(), "EntityArchetypeのChunk番号が範囲外です");
	Entity moved = chunks_[chunkIndex]->RemoveSwap(row);
	if (chunkIndex < firstWritableChunkIndex_) {

		firstWritableChunkIndex_ = chunkIndex;
	}
	return moved;
}

void Engine::EntityArchetype::ConstructDefault(uint32_t chunkIndex, uint32_t row, uint32_t typeID) {

	Assert::Call(chunkIndex < GetChunkCount(), "EntityArchetypeのChunk番号が範囲外です");
	chunks_[chunkIndex]->ConstructDefaultByColumnIndex(GetColumnIndex(typeID), row);
}

bool Engine::EntityArchetype::Has(uint32_t typeID) const {

	return typeID < typeToColumn_.size() && typeToColumn_[typeID] != kInvalidColumnIndex;
}

uint32_t Engine::EntityArchetype::GetColumnIndex(uint32_t typeID) const {

	Assert::Call(Has(typeID), "EntityArchetypeに指定ComponentTypeがありません");
	return typeToColumn_[typeID];
}

void* Engine::EntityArchetype::GetRaw(int32_t chunkIndex, uint32_t row, uint32_t typeID) {

	return chunks_[chunkIndex]->GetRawByColumnIndex(GetColumnIndex(typeID), row);
}

const void* Engine::EntityArchetype::GetRaw(
	int32_t chunkIndex, uint32_t row, uint32_t typeID) const {

	return chunks_[chunkIndex]->GetRawByColumnIndex(
		GetColumnIndex(typeID), row);
}

uint32_t Engine::EntityArchetype::GetAllocatedChunkCount() const {

	uint32_t count = 0;
	for (const auto& chunk : chunks_) {
		count += chunk->IsAllocated() ? 1u : 0u;
	}
	return count;
}

size_t Engine::EntityArchetype::GetAllocatedBytes() const {

	size_t bytes = 0;
	for (const auto& chunk : chunks_) {
		bytes += chunk->GetAllocatedBytes();
	}
	return bytes;
}

size_t Engine::EntityArchetype::GetPayloadBytes() const {

	size_t bytes = 0;
	for (const auto& chunk : chunks_) {
		bytes += chunk->GetPayloadBytes();
	}
	return bytes;
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
	chunks_.push_back(std::make_unique<EntityChunk>(&chunkLayout_));
	firstWritableChunkIndex_ = GetChunkCount() - 1;
	return firstWritableChunkIndex_;
}
