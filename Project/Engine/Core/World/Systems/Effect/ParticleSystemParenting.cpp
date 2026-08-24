#include "ParticleSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>
#include <Engine/Core/Foundation/Math/AffineDecompose.h>

// c++
#include <cmath>

namespace {

	bool DecomposeParentMatrix(const Engine::Matrix4x4& matrix,
		Engine::Quaternion& outRotation, Engine::Vector3& outScale) {

		Engine::Vector3 translation{};
		return Engine::DecomposeAffine3D(matrix, translation, outRotation, outScale);
	}

	Engine::Vector3 DivideScale(const Engine::Vector3& value, const Engine::Vector3& divisor) {

		constexpr float kMinScale = 1.0e-6f;
		return Engine::Vector3(
			std::abs(divisor.x) <= kMinScale ? value.x : value.x / divisor.x,
			std::abs(divisor.y) <= kMinScale ? value.y : value.y / divisor.y,
			std::abs(divisor.z) <= kMinScale ? value.z : value.z / divisor.z);
	}

	void BakeParticleParentToWorld(Engine::Particle& particle) {

		if (!particle.hasParent) {
			return;
		}

		particle.pos = Engine::Vector3::Transform(particle.pos, particle.parentMatrix);
		particle.velocity = Engine::Vector3::TransferNormal(particle.velocity, particle.parentMatrix);
		particle.spawnDirection = Engine::Vector3::NormalizeOr(
			Engine::Vector3::TransferNormal(particle.spawnDirection, particle.parentMatrix),
			Engine::Vector3(0.0f, 1.0f, 0.0f));

		Engine::Quaternion parentRotation{};
		Engine::Vector3 parentScale{};
		if (DecomposeParentMatrix(particle.parentMatrix, parentRotation, parentScale)) {
			particle.rotation = Engine::Quaternion::Normalize(parentRotation * particle.rotation);
			particle.scale = parentScale * particle.scale;
		}

		particle.parentMatrix = Engine::Matrix4x4::Identity();
		particle.parentLocalFileID = {};
		particle.parentIsEmitter = false;
		particle.hasParent = false;
	}

	void AttachParticleParent(Engine::Particle& particle, const Engine::Matrix4x4& parentMatrix,
		const Engine::Quaternion& parentRotation, const Engine::Vector3& parentScale,
		bool parentIsEmitter, Engine::UUID parentLocalFileID, bool preserveWorldRotationScale) {

		const Engine::Matrix4x4 inverseParent = Engine::Matrix4x4::Inverse(parentMatrix);
		particle.pos = Engine::Vector3::Transform(particle.pos, inverseParent);
		particle.velocity = Engine::Vector3::TransferNormal(particle.velocity, inverseParent);
		particle.spawnDirection = Engine::Vector3::NormalizeOr(
			Engine::Vector3::TransferNormal(particle.spawnDirection, inverseParent),
			Engine::Vector3(0.0f, 1.0f, 0.0f));
		if (preserveWorldRotationScale) {
			particle.rotation = Engine::Quaternion::Normalize(
				Engine::Quaternion::Inverse(parentRotation) * particle.rotation);
			particle.scale = DivideScale(particle.scale, parentScale);
		}

		particle.parentMatrix = parentMatrix;
		particle.parentLocalFileID = parentLocalFileID;
		particle.parentIsEmitter = parentIsEmitter;
		particle.hasParent = true;
	}

	void DetachParticleParentWithoutKeepingWorld(Engine::Particle& particle) {

		particle.parentMatrix = Engine::Matrix4x4::Identity();
		particle.parentLocalFileID = {};
		particle.parentIsEmitter = false;
		particle.hasParent = false;
	}
}

//============================================================================
//	ParticleSystem parenting methods
//============================================================================
const Engine::ParticlePhaseParentSettings& Engine::ParticleSystem::ResolveParticleParentSettings(
	const PhaseRuntime& phase, const ParticlePhaseParentSettings& parentSettings,
	bool useAssetParentSettings) const {

	return useAssetParentSettings ? phase.parentSettings : parentSettings;
}

void Engine::ParticleSystem::UpdateParticleParent(Particle& particle,
	const ParticlePhaseParentSettings& settings, const ParentRuntime& parent,
	bool preserveWorldRotationScale) const {

	if (!parent.resolved) {

		if (particle.hasParent) {
			if (settings.keepWorldOnDetach) {
				BakeParticleParentToWorld(particle);
			} else {
				DetachParticleParentWithoutKeepingWorld(particle);
			}
		}
		return;
	}

	const bool sameParent = particle.hasParent &&
		particle.parentIsEmitter == settings.useEmitter &&
		(settings.useEmitter || particle.parentLocalFileID == settings.entityLocalFileID);
	if (sameParent) {
		particle.parentMatrix = parent.matrix;
		return;
	}

	if (particle.hasParent) {
		BakeParticleParentToWorld(particle);
	}
	AttachParticleParent(particle, parent.matrix, parent.rotation, parent.scale, settings.useEmitter,
		settings.useEmitter ? UUID{} : settings.entityLocalFileID, preserveWorldRotationScale);
}

void Engine::ParticleSystem::UpdateParticleParents(std::vector<Particle>& particles,
	const GroupRuntime& group, const ParticlePhaseParentSettings& parentSettings,
	bool useAssetParentSettings, const std::vector<ParentRuntime>& parents) const {

	for (Particle& particle : particles) {

		if (particle.phaseIndex < group.phases.size()) {
			const PhaseRuntime& phase = group.phases[particle.phaseIndex];
			UpdateParticleParent(particle, ResolveParticleParentSettings(
				phase, parentSettings, useAssetParentSettings), parents[particle.phaseIndex]);
		} else if (particle.hasParent) {
			BakeParticleParentToWorld(particle);
		}
		const ParentRuntime* parent = particle.phaseIndex < parents.size() ?
			&parents[particle.phaseIndex] : nullptr;
		RefreshParticleWorldTransform(particle, parent);
	}
}

void Engine::ParticleSystem::ResolveParticleParents(ECSWorld& world, const Matrix4x4& emitterWorld,
	const GroupRuntime& group, const ParticlePhaseParentSettings& parentSettings,
	bool useAssetParentSettings, std::vector<ParentRuntime>& outParents) const {

	constexpr float kMinScale = 1.0e-6f;
	for (size_t i = 0; i < group.phases.size(); ++i) {

		const ParticlePhaseParentSettings& settings = ResolveParticleParentSettings(
			group.phases[i], parentSettings, useAssetParentSettings);
		if (!settings.HasParent()) {
			continue;
		}
		Matrix4x4 parentWorld = emitterWorld;
		if (!settings.useEmitter) {

			const Entity parentEntity = SceneObjectUtility::FindByLocalFileID(world, settings.entityLocalFileID);
			if (!world.IsAlive(parentEntity)) { continue; }
			ResolvedWorldTransform parentTransform{};
			if (!TransformWorldUtility::ResolveWorldTransform(
				world, parentEntity, parentTransform)) {
				continue;
			}
			parentWorld = parentTransform.matrix;
		}

		ParentRuntime& parent = outParents[i];
		parent.matrix = BuildParentFollowMatrix(parentWorld,
			settings.ignoreParentScale, settings.ignoreParentRotation);
		if (!DecomposeParentMatrix(parent.matrix, parent.rotation, parent.scale) ||
			std::abs(parent.scale.x) <= kMinScale ||
			std::abs(parent.scale.y) <= kMinScale ||
			std::abs(parent.scale.z) <= kMinScale) {
			continue;
		}
		parent.resolved = true;
	}
}

void Engine::ParticleSystem::RefreshParticleWorldTransform(
	Particle& particle, const ParentRuntime* parent) const {

	if (!particle.hasParent || !parent || !parent->resolved) {

		particle.worldPos = particle.pos;
		particle.worldRotation = particle.rotation;
		particle.worldScale = particle.scale;
		return;
	}

	particle.worldPos = Vector3::Transform(particle.pos, particle.parentMatrix);
	particle.worldRotation = Quaternion::Normalize(parent->rotation * particle.rotation);
	particle.worldScale = parent->scale * particle.scale;
}
