#include "ParticleFeedbackTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleGPUFeedbackTypes.h>
#include <Engine/Core/World/Components/Rendering/ParticlePaintReceiverComponent.h>

// c++
#include <array>
#include <limits>

bool NEMTests::TestParticleFeedbackSettings() {

	using namespace Engine;
	const std::array<Entity, 4> sources{Entity{1, 0}, Entity{2, 3}, Entity{3, 0}, Entity{4, 1}};
	std::array<std::vector<Entity>, 4> targets;
	targets[0] = {sources[1]};
	targets[1] = {sources[2]};
	targets[2] = {Entity{2, 2}};
	if (HasParticleEventCycle(sources, targets)) return false;
	targets[2] = {sources[0]};
	if (!HasParticleEventCycle(sources, targets)) return false;
	targets[2].clear();
	targets[3] = {sources[3]};
	if (!HasParticleEventCycle(sources, targets)) return false;
	if (!HasParticleEventCycle(sources, std::span<const std::vector<Entity>>(targets).first(3))) return false;

	ParticleFeedbackSettings settings;
	settings.eventCapacity = UINT32_MAX;
	settings.events.resize(100);
	settings.events[0].count = UINT32_MAX;
	settings.events[0].time = std::numeric_limits<float>::infinity();
	settings.events[0].probability = -1;
	settings.paint.radius = -5;
	settings.decal.capacity = UINT32_MAX;
	settings.decal.size.x = std::numeric_limits<float>::quiet_NaN();
	settings.decal.lifetime = -1;
	settings.decal.fadeDuration = 99;
	SanitizeParticleFeedbackSettings(settings);
	if (settings.eventCapacity != 65536 || settings.events.size() != 32 || settings.events[0].count != 256 ||
		settings.events[0].time != 0.5f || settings.events[0].probability != 0 || settings.paint.radius != 0.001f ||
		settings.decal.capacity != 16384 || settings.decal.size.x != 0.2f || settings.decal.fadeDuration > settings.decal.lifetime) return false;
	settings.events[0].kind = ParticleEventKind::PhaseTransition;
	settings.events[0].target.localFileID = Engine::UUID::New();
	settings.events[0].inheritVelocity = true;
	settings.paint.enabled = true;
	settings.decal.enabled = true;
	nlohmann::json saved;
	to_json(saved, settings);
	ParticleFeedbackSettings restored;
	from_json(saved, restored);
	nlohmann::json actual;
	to_json(actual, restored);
	if (saved != actual) return false;

	ParticlePaintReceiverComponent receiver;
	receiver.clearSerial = 17;
	to_json(saved, receiver);
	if (saved.contains("clearSerial")) return false;
	saved["resolution"] = UINT32_MAX;
	saved["clearSerial"] = 99;
	ParticlePaintReceiverComponent restoredReceiver;
	from_json(saved, restoredReceiver);
	return restoredReceiver.resolution == 2048 && restoredReceiver.clearSerial == 0;
}
