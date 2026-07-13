#include "FlipbookAnimationSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/FlipbookAnimationComponent.h>
#include <Engine/Core/World/Components/Rendering/UVTransformComponent.h>
#include <Engine/Core/Foundation/Utility/Flipbook/FlipbookFrame.h>

// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	FlipbookAnimationSystem classMethods
//============================================================================
void Engine::FlipbookAnimationSystem::Update(ECSWorld& world, SystemContext& context) {

	world.ForEach<FlipbookAnimationComponent>([&](const Entity& entity, FlipbookAnimationComponent& flipbook) {

		// UVTransform必須、無ければ何もしない
		auto* uvTransform = world.TryGetComponent<UVTransformComponent>(entity);
		if (!uvTransform) {
			return;
		}

		// 再生を進めるか、Play中はTimeScale適用済みのdeltaTime、Editのプレビューはリアル時間を使う
		const bool allowTimeAdvance = flipbook.enabled &&
			(context.mode == WorldMode::Play || flipbook.playInEditMode);
		const float deltaTime = (context.mode == WorldMode::Play) ? context.deltaTime : context.unscaledDeltaTime;
		if (!allowTimeAdvance || deltaTime <= 0.0f) {
			return;
		}

		// 経過時間を進め、ループか終了を判定する
		const float duration = (std::max)(flipbook.duration, 0.001f);
		flipbook.runtimeElapsed += deltaTime;
		float progress = 0.0f;
		if (flipbook.loop) {

			const float loopInterval = (std::max)(flipbook.loopInterval, 0.0f);
			const float loopDuration = duration + loopInterval;
			if (loopDuration <= flipbook.runtimeElapsed) {

				flipbook.runtimeRepeatCount += static_cast<int32_t>(flipbook.runtimeElapsed / loopDuration);
				flipbook.runtimeElapsed = std::fmod(flipbook.runtimeElapsed, loopDuration);
			}

			const bool waitingNextLoop = duration <= flipbook.runtimeElapsed;
			progress = waitingNextLoop ? 1.0f : flipbook.runtimeElapsed / duration;
			flipbook.runtimePlaying = !waitingNextLoop;
			flipbook.runtimeAnimationFinished = false;
		} else {

			flipbook.runtimePlaying = true;
			progress = flipbook.runtimeElapsed / duration;
			if (1.0f <= progress) {

				progress = 1.0f;
				flipbook.runtimePlaying = false;
				flipbook.runtimeAnimationFinished = true;
			}
		}

		// 再生終了後に何も表示させない場合はUVを潰す
		if (flipbook.runtimeAnimationFinished && flipbook.endAnimUnDisplay) {

			uvTransform->scale = Vector2::AnyInit(0.0f);
			return;
		}

		// イージングを掛けた進行度からコマを求めてUVへ反映する
		const FlipbookFrame frame = CalcFlipbookFrame(flipbook.tilesX, flipbook.tilesY, EasedValue(flipbook.easingType, progress));
		uvTransform->scale = frame.uvScale;
		uvTransform->pos = frame.uvOffset;
		});
}
