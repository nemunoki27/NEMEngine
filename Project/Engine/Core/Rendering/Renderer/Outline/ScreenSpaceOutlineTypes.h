#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/ScreenSpaceOutlineComponent.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Foundation/Math/Color.h>

// c++
#include <cstdint>

namespace Engine {

	// front
	class ECSWorld;

	//============================================================================
	//	ScreenSpaceOutline request types
	//============================================================================

	// アウトライン要求の発生源
	enum class ScreenSpaceOutlineSource :
		uint8_t {

		// エンティティ適用コンポーネント
		RuntimeComponent,
		// エディタ選択
		EditorSelection,
	};

	// アウトラインの見た目
	struct ScreenSpaceOutlineStyle {

		Color4 color = Color4::FromHex(0xFF8000FF);
		float widthPixels = 3.0f;
		int32_t priority = 0;
		ScreenSpaceOutlineVisibilityMode visibilityMode = ScreenSpaceOutlineVisibilityMode::VisibleOnly;
		ScreenSpaceOutlineRegionMode regionMode = ScreenSpaceOutlineRegionMode::AllVisibleSilhouettes;
	};

	// 1つのアウトライン描画要求
	struct ScreenSpaceOutlineRequest {

		ECSWorld* world = nullptr;
		Entity entity = Entity::Null();

		// 負ならEntity全体、0以上なら対象SubMeshのみ
		int32_t subMeshIndex = -1;

		ScreenSpaceOutlineStyle style{};
		ScreenSpaceOutlineSource source = ScreenSpaceOutlineSource::RuntimeComponent;
	};
} // Engine