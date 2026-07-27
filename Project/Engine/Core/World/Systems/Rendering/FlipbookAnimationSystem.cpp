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

	world.ForEach<FlipbookAnimationComponent, FlipbookAnimationRuntimeComponent>(
		[&](const Entity& entity, FlipbookAnimationComponent& flipbook,
			FlipbookAnimationRuntimeComponent& runtime) {

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
		runtime.elapsed += deltaTime;
		float progress = 0.0f;
		if (flipbook.loop) {

			const float loopInterval = (std::max)(flipbook.loopInterval, 0.0f);
			const float loopDuration = duration + loopInterval;
			if (loopDuration <= runtime.elapsed) {

				runtime.repeatCount += static_cast<int32_t>(runtime.elapsed / loopDuration);
				runtime.elapsed = std::fmod(runtime.elapsed, loopDuration);
			}

			const bool waitingNextLoop = duration <= runtime.elapsed;
			progress = waitingNextLoop ? 1.0f : runtime.elapsed / duration;
			runtime.playing = !waitingNextLoop;
			runtime.animationFinished = false;
		} else {

			runtime.playing = true;
			progress = runtime.elapsed / duration;
			if (1.0f <= progress) {

				progress = 1.0f;
				runtime.playing = false;
				runtime.animationFinished = true;
			}
		}

		// 再生終了後に何も表示させない場合はUVを潰す
		if (runtime.animationFinished && flipbook.endAnimUnDisplay) {

			uvTransform->scale = Vector2::AnyInit(0.0f);
			return;
		}

		// イージングを掛けた進行度からコマを求めてUVへ反映する
		const FlipbookFrame frame = CalcFlipbookFrame(
			GetFlipbookTileValues(world, entity), flipbook.tilesY,
			EasedValue(flipbook.easingType, progress));
		uvTransform->scale = frame.uvScale;
		uvTransform->pos = frame.uvOffset;
		});
}
