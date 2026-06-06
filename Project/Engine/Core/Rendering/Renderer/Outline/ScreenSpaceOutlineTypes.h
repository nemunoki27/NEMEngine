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
	enum class ScreenSpaceOutlineSource : uint8_t {

		// ScreenSpaceOutlineComponent由来(Scene保存対象、Game/Scene両方)
		RuntimeComponent,
		// エディタ選択由来(temporary、Sceneのみ)
		EditorSelection,
	};

	// アウトラインの見た目。Runtime ComponentとEditor選択で共通の描画パラメータ
	struct ScreenSpaceOutlineStyle {

		Color4 color{};
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
