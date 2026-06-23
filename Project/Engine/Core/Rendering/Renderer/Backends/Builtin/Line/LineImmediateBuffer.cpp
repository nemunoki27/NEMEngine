#include "LineImmediateBuffer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineShapeBuilder.h>

//============================================================================
//	LineImmediateBuffer classMethods
//============================================================================
Engine::LineImmediateBuffer& Engine::LineImmediateBuffer::GetInstance() {

	static LineImmediateBuffer instance;
	return instance;
}

void Engine::LineImmediateBuffer::BeginFrame() {

	points_.clear();
	entries_.clear();
}

void Engine::LineImmediateBuffer::AddPolyline(const LinePoint* points, uint32_t count,
	bool connected, bool loop, bool is2D, AssetID material) {

	if (points == nullptr || count < 2) {
		return;
	}

	Entry entry{};
	entry.pointOffset = static_cast<uint32_t>(points_.size());
	entry.pointCount = count;
	entry.connected = connected;
	entry.loop = loop;
	entry.is2D = is2D;
	entry.material = material;
	entries_.emplace_back(entry);

	points_.insert(points_.end(), points, points + count);
}

void Engine::LineImmediateBuffer::AddSphere(const Vector3& center, float radius, const Color4& color,
	uint32_t division, float thickness, AssetID material) {

	const uint32_t startOffset = static_cast<uint32_t>(points_.size());
	// 線分リストとしてプールへ直接展開する
	LineShapeBuilder::BuildSphere(center, radius, color, division, thickness, points_);

	const uint32_t addedCount = static_cast<uint32_t>(points_.size()) - startOffset;
	if (addedCount < 2) {
		return;
	}

	Entry entry{};
	entry.pointOffset = startOffset;
	entry.pointCount = addedCount;
	entry.connected = false;
	entry.material = material;
	entries_.emplace_back(entry);
}
