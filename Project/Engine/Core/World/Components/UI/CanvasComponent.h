#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Runtime/Context/EngineContext.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Foundation/Math/Vector2.h>

namespace Engine {

	//============================================================================
	//	CanvasComponent struct
	//	スクリーンUIの基準解像度と入力設定を管理する
	//============================================================================
	enum class CanvasScaleMode :
		uint8_t {

		ConstantPixelSize,
		ScaleWithScreenSize,
	};

	struct CanvasComponent {

		bool enabled = true;

		// ゲーム解像度
		Vector2 referenceResolution = EngineContext::GetWindowSetting().gameSizeFloat;
		CanvasScaleMode scaleMode = CanvasScaleMode::ScaleWithScreenSize;
		float scaleFactor = 1.0f;
		float matchWidthOrHeight = 0.5f;

		int32_t sortingLayer = 0;
		int32_t order = 0;

		bool blockGameplayInput = true;
		bool mouseHoverSelect = true;
		bool wrapNavigation = true;
		float repeatDelay = 0.35f;
		float repeatInterval = 0.12f;
		float stickThreshold = 0.5f;

		UUID firstSelectedLocalFileID{};

		// ランタイム入力状態
		UUID runtimeSelectedLocalFileID{};
		UUID runtimeHoveredLocalFileID{};
		UUID runtimePressedLocalFileID{};
		Vector2 runtimeRepeatDirection{};
		float runtimeRepeatElapsed = 0.0f;
		bool runtimeRepeatStarted = false;
	};

	void from_json(const nlohmann::json& in, CanvasComponent& component);
	void to_json(nlohmann::json& out, const CanvasComponent& component);

	ENGINE_REGISTER_COMPONENT(CanvasComponent, "Canvas");
} // Engine
