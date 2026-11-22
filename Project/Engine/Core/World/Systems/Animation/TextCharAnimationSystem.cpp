#include "TextCharAnimationSystem.h"

#include <Engine/Core/Animation/Clips/AnimationClipAssetOverride.h>
#include <Engine/Core/Animation/Text/TextCharAnimationSampler.h>
#include <Engine/Core/Animation/Text/TextCharAnimationTiming.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Text/TextLayoutBuilder.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

	using namespace Engine;

	void ResetPlayback(TextCharAnimationComponent& c, bool play) {

		c.runtimeClock = 0;
		c.runtimeTime = 0;
		c.runtimePlaying = play;
		c.runtimeEngaged = play;
		c.runtimePaused = false;
		c.runtimeFinished = false;
		c.runtimeApplied = false;
		c.runtimePoses.clear();
	}

	void UpdateText(ECSWorld& world, SystemContext& context, Entity owner, TextCharAnimationComponent& c) {

		const bool edit = context.mode == WorldMode::Edit;
		const int request = std::exchange(c.runtimeRequest, 0);
		auto* renderer = world.TryGetComponent<TextRendererComponent>(owner);
		c.runtimeDiagnostic.clear();
		if (c.legacyClip && (!context.assetDatabase || !context.animationClipManager ||
			!ResolveLegacyTextCharClip(world, *context.assetDatabase, *context.animationClipManager, c))) {
			c.runtimeApplied = false;
			c.runtimeDuration = 0;
			c.runtimeDiagnostic = "参照先のアニメーションクリップが見つかりません";
			return;
		}
		if (!c.enabled || !IsEntityActiveInHierarchy(world, owner) || !renderer) {
			ResetPlayback(c, false);
			c.runtimeInitialized = false;
			c.runtimeDuration = 0;
			if (!renderer) c.runtimeDiagnostic = "同じオブジェクトにTextRendererが必要です";
			return;
		}

		bool start = !c.runtimeInitialized || c.runtimeEdit != edit || (edit && c.runtimeEditAuto != c.playInEditMode);
		if (start || request == 1 || request == 4) {
			const bool autoPlay = edit ? c.playInEditMode : c.playOnStart;
			ResetPlayback(c, request == 1 || (start && autoPlay && request != 4));
			c.runtimeInitialized = true;
			c.runtimeEdit = edit;
			c.runtimeEditAuto = c.playInEditMode;
			start = true;
		}
		if (request == 2 && c.runtimePlaying) {
			c.runtimePlaying = false;
			c.runtimePaused = true;
		}
		if (request == 3 && c.runtimePaused) {
			c.runtimePlaying = true;
			c.runtimePaused = false;
		}
		if (request == 6) {
			c.runtimeClock = std::isfinite(c.runtimeSeekTime) ? (std::max)(0.0f, c.runtimeSeekTime) : 0;
			c.runtimeEngaged = true;
			c.runtimePaused = !c.runtimePlaying;
			c.runtimeFinished = false;
			start = true;
		}

		TextCharAnimationSampler sampler;
		if (!sampler.Prepare(world, owner, context, c, c.runtimeDiagnostic)) {
			c.runtimeApplied = false;
			c.runtimeDuration = 0;
			return;
		}
		if (context.renderAssetLibrary && renderer->font) {
			if (const auto* font = context.renderAssetLibrary->LoadFont(renderer->font);
				font && TextLayoutBuilder::NeedsTextLayoutRebuild(world, owner, *renderer, *font)) {
				TextLayoutBuilder::RebuildTextLayoutCache(*font, world, owner, *renderer);
			}
		}
		const auto* layout = world.TryGetComponent<TextLayoutRuntimeComponent>(owner);
		const auto glyphs = GetTextLayoutGlyphs(std::as_const(world), owner);
		if (!layout || !layout->valid || glyphs.empty()) {
			c.runtimeApplied = false;
			c.runtimeDuration = 0;
			return;
		}
		UpdateTextCharSchedule(c.runtimeSchedule, c, glyphs, layout->layoutRevision, sampler.Duration());
		c.runtimeDuration = c.runtimeSchedule.duration;
		if (!c.runtimeEngaged) return;

		const auto time = ResolveLocalTime(world, owner, c.timeSource, sampler.Channels(),
			edit ? context.unscaledDeltaTime : context.deltaTime, context.unscaledDeltaTime);
		if (!time.valid) c.runtimeDiagnostic = "時間源のオブジェクトまたはTimeScaleが見つかりません";
		if (time.valid && !start && c.runtimePlaying) c.runtimeClock += time.deltaTime;

		if (c.repeat == TextCharAnimationRepeat::Once && c.runtimeClock >= c.runtimeDuration) {
			c.runtimeClock = c.runtimeDuration;
			c.runtimeFinished = true;
			c.runtimePlaying = c.runtimePaused = false;
		} else if (c.repeat == TextCharAnimationRepeat::LoopText && c.runtimeDuration > 0) {
			c.runtimeClock = std::fmod(c.runtimeClock, c.runtimeDuration);
		}
		c.runtimeTime = static_cast<float>(c.runtimeClock);
		c.runtimePoses.resize(glyphs.size());
		for (size_t i = 0; i < glyphs.size(); ++i) {
			const float local = (std::max)(0.0f, c.runtimeTime - c.runtimeSchedule.starts[i]);
			c.runtimePoses[i] = sampler.Sample(local, c.repeat == TextCharAnimationRepeat::LoopCharacters);
		}
		c.runtimeLayoutRevision = layout->layoutRevision;
		c.runtimeApplied = true;
	}
}

bool Engine::ResolveLegacyTextCharClip(ECSWorld& world, AssetDatabase& database, AnimationClipManager& clips,
	TextCharAnimationComponent& component) {

	if (!component.legacyClip) return true;
	const auto* clip = ResolveAnimationClipAsset(world, clips, database, component.legacyClip);
	if (!clip) return false;
	const auto mask = TextCharAnimationSampler::ClipChannelMask(*clip);
	for (size_t i = 0; i < component.sourceAssets.size(); ++i) {
		if (mask & (1u << i)) component.sourceAssets[i] = component.legacyClip;
	}
	component.legacyClip = {};
	return true;
}

void Engine::TextCharAnimationSystem::Update(ECSWorld& world, SystemContext& context) {

	world.ForEach<TextCharAnimationComponent>([&](Entity owner, TextCharAnimationComponent& component) {
		UpdateText(world, context, owner, component);
	});
}
