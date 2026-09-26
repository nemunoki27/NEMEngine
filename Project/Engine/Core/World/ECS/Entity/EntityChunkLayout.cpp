#include "EntityChunk.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>

// c++
#include <stdexcept>

namespace {

	// Chunk内へ整列した領域を追加する
	bool TryAppend(size_t alignment, size_t elementSize, size_t count, size_t& offset, size_t& begin) {

		const size_t padding = (alignment - offset % alignment) % alignment;
		if (padding > Engine::kChunkBytes - offset) {
			return false;
		}
		offset += padding;
		begin = offset;
		if (elementSize != 0 && count > (Engine::kChunkBytes - offset) / elementSize) {
			return false;
		}
		offset += elementSize * count;
		return true;
	}

	// 値、個体番号、有効状態を固定長Chunkへ配置する
	bool TryBuildLayout(const std::vector<uint32_t>& types, uint32_t capacity, Engine::EntityChunkLayout& out) {

		auto& registry = Engine::ComponentTypeRegistry::GetInstance();
		size_t alignment = (std::max)(Engine::kChunkAlignment, alignof(Engine::Entity));
		size_t offset = 0;
		out.columns.clear();
		out.columns.reserve(types.size());
		if (!TryAppend(alignof(Engine::Entity), sizeof(Engine::Entity), capacity, offset, out.entityOffset)) {
			return false;
		}

		for (uint32_t typeID : types) {

			const auto& info = registry.GetInfo(typeID);
			alignment = (std::max)(alignment, info.align);
			Engine::EntityColumnLayout column{};
			column.typeID = typeID;
			column.info = &info;
			if (!TryAppend(info.align, info.size, capacity, offset, column.offset) ||
				!TryAppend(alignof(uint64_t), sizeof(uint64_t), capacity, offset, column.instanceOffset)) {
				return false;
			}
			if (info.enableable &&
				!TryAppend(alignof(uint64_t), sizeof(uint64_t), (capacity + 63u) / 64u, offset, column.enabledOffset)) {
				return false;
			}
			out.columns.emplace_back(column);
		}
		out.bytes = Engine::kChunkBytes;
		out.alignment = alignment;
		out.capacity = capacity;
		return true;
	}
}

//============================================================================
//	EntityChunkLayout structMethods
//============================================================================
Engine::EntityChunkLayout Engine::EntityChunkLayout::Build(const std::vector<uint32_t>& types) {

	auto& registry = ComponentTypeRegistry::GetInstance();
	size_t bytesPerEntity = sizeof(Entity);
	for (uint32_t typeID : types) {

		const size_t size = registry.GetInfo(typeID).size;
		if (size > kChunkBytes - sizeof(uint64_t) || size + sizeof(uint64_t) > kChunkBytes - bytesPerEntity) {
			throw std::length_error("Archetypeの1Entity分のデータがChunkサイズを超えています");
		}
		bytesPerEntity += size + sizeof(uint64_t);
	}

	uint32_t capacity = static_cast<uint32_t>((std::min)(kChunkBytes / bytesPerEntity, size_t{ kMaxChunkEntities }));
	EntityChunkLayout layout{};
	while (capacity != 0) {
		if (TryBuildLayout(types, capacity, layout)) {
			return layout;
		}
		--capacity;
	}
	throw std::length_error("Archetypeの整列済みデータがChunkサイズを超えています");
}

size_t Engine::EntityChunkLayout::GetPayloadBytes(uint32_t count) const {

	size_t bytesPerEntity = sizeof(Entity);
	size_t enabledBytes = 0;
	for (const EntityColumnLayout& column : columns) {
		bytesPerEntity += column.info->size + sizeof(uint64_t);
		if (column.info->enableable) {
			enabledBytes += sizeof(uint64_t) * ((count + 63u) / 64u);
		}
	}
	return bytesPerEntity * count + enabledBytes;
}
