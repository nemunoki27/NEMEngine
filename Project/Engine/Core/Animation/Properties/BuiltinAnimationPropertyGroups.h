#pragma once

namespace Engine {

	class AnimationPropertyRegistry;

	// Transformの編集値を登録する
	void RegisterTransformAnimationProperties(AnimationPropertyRegistry& registry);

	// Spriteの編集値を登録する
	void RegisterSpriteAnimationProperties(AnimationPropertyRegistry& registry);

	// Textの編集値を登録する
	void RegisterTextAnimationProperties(AnimationPropertyRegistry& registry);

	// Lightの編集値を登録する
	void RegisterLightingAnimationProperties(AnimationPropertyRegistry& registry);

	// Cameraの編集値を登録する
	void RegisterCameraAnimationProperties(AnimationPropertyRegistry& registry);

	// Camera追従の編集値を登録する
	void RegisterCameraControllerAnimationProperties(AnimationPropertyRegistry& registry);

	// 音声とUVと骨再生の編集値を登録する
	void RegisterRuntimeAnimationProperties(AnimationPropertyRegistry& registry);

	// 衝突形状の編集値を登録する
	void RegisterCollisionAnimationProperties(AnimationPropertyRegistry& registry);

	// SubMeshのUVの編集値を登録する
	void RegisterMeshUVAnimationProperties(AnimationPropertyRegistry& registry);
} // Engine
