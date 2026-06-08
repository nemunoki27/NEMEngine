#include "SceneComponentOverlayPicker.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/SceneComponentOverlay/SceneComponentOverlayState.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>

//============================================================================
//	local
//============================================================================
namespace {

	struct Hit {

		// Entity選択用のヒット情報。サブメッシュ選択は行わない
		Engine::Entity entity = Engine::Entity::Null();
		float depth = 0.0f;
		uint32_t stableOrder = 0;
	};

	// Overlayアイコンは描画矩形と同じ矩形でCPUピックする
	bool Contains(const Engine::SceneComponentOverlayItem& item, const Engine::Vector2& point) {

		return item.rectMin.x <= point.x && point.x <= item.rectMax.x &&
			item.rectMin.y <= point.y && point.y <= item.rectMax.y;
	}
}

//============================================================================
//	SceneComponentOverlayPicker classMethods
//============================================================================
bool Engine::SceneComponentOverlayPicker::Pick(ECSWorld* world, const ResolvedRenderView& view,
	const Vector2& inputPixel, Entity& outEntity) const {

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	outEntity = Entity::Null();
	if (!world || !view.valid) {
		return false;
	}

	// GameViewやPreviewの状態を拾わないよう、描画済みWorldと一致する時だけ使う
	const SceneComponentOverlayState& state = SceneComponentOverlayState::GetInstance();
	if (state.GetWorld() != world) {
		return false;
	}

	std::vector<Hit> hits{};

	// 実際に描画されたOverlayだけを候補にする
	for (const SceneComponentOverlayItem& item : state.GetRenderedItems()) {
		if (!world->IsAlive(item.entity)) {
			continue;
		}

		switch (item.kind) {
		case SceneComponentOverlayKind::LightIcon:
		case SceneComponentOverlayKind::CameraIcon:
			if (Contains(item, inputPixel)) {
				hits.push_back({ item.entity, item.viewDepth, item.stableOrder });
			}
			break;
		}
	}

	if (hits.empty()) {
		return false;
	}

	// View深度が近いものを優先し、同値はstableOrderで決定的に選ぶ
	std::sort(hits.begin(), hits.end(), [](const Hit& lhs, const Hit& rhs) {
		if (lhs.depth != rhs.depth) {
			return lhs.depth < rhs.depth;
		}
		return lhs.stableOrder < rhs.stableOrder;
	});

	outEntity = hits.front().entity;
	return outEntity.IsValid();
#else
	(void)world;
	(void)view;
	(void)inputPixel;
	outEntity = Entity::Null();
	return false;
#endif
}
