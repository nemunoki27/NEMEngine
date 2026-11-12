#include "TimelineEditorSession.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Timeline/TimelinePlayback.h>
#include <Engine/Core/Animation/Timeline/TimelinePlaybackState.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#include <Engine/Core/World/Components/Animation/MotionAnimationComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/World/ECSWorldLifetime.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/Systems/Animation/JointAttachmentSystem.h>
#include <Engine/Core/World/Systems/Transform/TransformSystem.h>
#include <Engine/Editor/Utility/CollisionDebugDraw.h>

// c++
#include <algorithm>
#include <cmath>
#include <utility>

using namespace Engine;

namespace Engine {

	struct TimelineEditorPreviewState {

		ECSWorld* world = nullptr;
		std::weak_ptr<const ECSWorldLifetime> lifetime;
		Entity owner = Entity::Null();
		SystemContext systems;
		std::shared_ptr<const TimelineAsset> asset;
		TimelinePlayerRecord player;
		TransformSystem transforms;
		JointAttachmentSystem attachments;
		uint64_t draftRevision = 0;
	};
}

TimelineEditorSession::~TimelineEditorSession() {

	EndPreview();
}

void TimelineEditorSession::EndPreview() {

	playing = false;
	const auto preview = std::move(preview_);
	if (!preview) return;
	const auto lifetime = preview->lifetime.lock();
	if (lifetime && lifetime->IsAlive()) {
		ScopedCleanup unsubscribe([&]() noexcept {
			if (lifetime->IsAlive()) preview->transforms.OnWorldExit(*preview->world, preview->systems);
		});
		ReleaseTimelinePlayer(*preview->world, preview->owner, preview->player,
			preview->systems, TimelineExitReason::Stopped);
		if (lifetime->IsAlive()) {
			preview->transforms.LateUpdate(*preview->world, preview->systems);
			preview->attachments.LateUpdate(*preview->world, preview->systems);
		}
	}
}

const std::string& TimelineEditorSession::GetPreviewDiagnostic() const {

	static const std::string empty;
	return preview_ ? preview_->player.runtime.diagnostic : empty;
}

float TimelineEditorSession::GetPreviewProgress() const {

	return preview_ ? preview_->player.runtime.preparationProgress : 1.0f;
}

void TimelineEditorSession::UpdatePreview(const EditorToolContext& context, double deltaTime) {

	if (!previewEnabled || context.IsPlaying() || !context.CanEditScene()) { EndPreview(); return; }
	if (playing && deltaTime > 0.0 && GetPreviewProgress() >= 1.0f) {
		time += deltaTime * speed;
		const double start = rangeEnabled ? (std::clamp)(rangeStart, 0.0, asset_.duration) : 0.0;
		const double end = rangeEnabled ? (std::clamp)(rangeEnd, start, asset_.duration) : asset_.duration;
		if (time >= end) {
			if ((loop || rangeEnabled) && end > start) time = start + std::fmod(time - start, end - start);
			else { time = end; playing = false; }
		}
	}
	EvaluatePreview(context);
}

void TimelineEditorSession::EvaluatePreview(const EditorToolContext& context) {

	ECSWorld* world = context.GetWorld();
	const Entity owner = GetOwner(context);
	if (!previewEnabled || !hasAsset_ || sourceMissing_ || !world || !world->IsAlive(owner) ||
		!context.toolContext.systemContext || context.IsPlaying()) { EndPreview(); return; }
	if (preview_ && (preview_->world != world || preview_->owner != owner)) {
		const bool wasPlaying = playing;
		EndPreview();
		playing = wasPlaying;
	}
	if (!preview_) {
		preview_ = std::make_shared<TimelineEditorPreviewState>();
		preview_->world = world;
		preview_->lifetime = world->GetLifetime();
		preview_->owner = owner;
		preview_->systems = *context.toolContext.systemContext;
		preview_->asset = std::make_shared<TimelineAsset>(asset_);
		preview_->player.settings = previewSettings_;
		preview_->player.settings.timeline = assetID_;
		preview_->player.owner = UUID::New();
		preview_->draftRevision = draftRevision_;
		preview_->transforms.OnWorldEnter(*world, preview_->systems);
	}
	if (preview_->draftRevision != draftRevision_) {
		preview_->asset = std::make_shared<TimelineAsset>(asset_);
		preview_->player.settings = previewSettings_;
		preview_->draftRevision = draftRevision_;
	}
	preview_->systems = *context.toolContext.systemContext;
	preview_->player.settings.timeSource.mode = TimeSourceMode::Unscaled;
	preview_->player.settings.speed = static_cast<float>(speed);
	preview_->player.runtime.playing = playing;
	preview_->player.runtime.paused = !playing;
	const auto preview = preview_;
	const auto lifetime = preview->lifetime.lock();
	EvaluateTimelineAt(*world, owner, preview->player, *preview->asset, time,
		preview->systems, TimelineEvaluationMode::Preview, false, scrubbing_);
	if (!lifetime || !lifetime->IsAlive() || preview_ != preview) return;
	preview->transforms.LateUpdate(*world, preview->systems);
	preview->attachments.LateUpdate(*world, preview->systems);
	if (const auto& state = preview->player.runtime.playback) {
		for (const auto& clip : state->clips) {
			if (!clip.entered || !clip.track || !clip.clip) continue;
			const auto& data = clip.clip->data;
			const auto debug = data.find("debug");
			if (clip.track->kind == TimelineTrackKind::Collision &&
				(debug == data.end() || (debug->is_boolean() && debug->get<bool>())))
				CollisionDebugDraw::DrawEntity(*world, clip.target);
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
			if (clip.instance) {
				const auto points = clip.instance->GetDebugPath();
				if (points.size() < 2) continue;
				auto* renderer = LineRenderer::GetInstance();
				const auto settings = data.find("settings");
				const auto plane = settings != data.end() && settings->is_object() ? settings->find("plane") : data.end();
				const bool planar = settings != data.end() && settings->is_object() && plane != settings->end() &&
					plane->is_number_integer() && plane->get<int32_t>() == static_cast<int32_t>(MotionAnimationPlane::PlaneXY);
				if (renderer) for (size_t i = 1; i < points.size(); ++i) {
					const Color4 color(0.25f, 0.9f, 1.0f, 1.0f);
					if (planar && renderer->Get2D()) renderer->Get2D()->DrawLine(
						Vector2(points[i - 1].x, points[i - 1].y), Vector2(points[i].x, points[i].y), color, 2.0f);
					else if (!planar && renderer->Get3D()) renderer->Get3D()->DrawLine(points[i - 1], points[i], color, 2.0f);
				}
			}
#endif
		}
	}
}
