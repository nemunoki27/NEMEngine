#include "ECSChangeTracker.h"

//============================================================================
//	include
//============================================================================

// c++
#include <algorithm>

using namespace Engine;

//============================================================================
//	ECSChangeTracker classMethods
//============================================================================
void ECSChangeTracker::MarkDataModified() {

	IncrementRevision(dataRevision_);
}

void ECSChangeTracker::MarkRenderDataModified() {

	IncrementRevision(renderDataRevision_);
	renderResetRevision_ = renderDataRevision_;
	entityRenderRevisions_.clear();
	meshColorRevisions_.clear();
	IncrementRevision(meshColorRevision_);
}

void ECSChangeTracker::MarkRenderDataModified(const Entity& entity) {

	IncrementRevision(renderDataRevision_);
	const uint64_t key = (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
	entityRenderRevisions_[key] = renderDataRevision_;
}

void ECSChangeTracker::MarkMeshColorModified(const Entity& entity) {

	IncrementRevision(meshColorRevision_);
	const uint64_t key = (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
	meshColorRevisions_[key] = meshColorRevision_;
	MarkDataModified();
}

uint64_t ECSChangeTracker::GetEntityRenderRevision(const Entity& entity) const {

	const uint64_t key = (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
	const auto found = entityRenderRevisions_.find(key);
	return found == entityRenderRevisions_.end() ? 0 : found->second;
}

uint64_t ECSChangeTracker::GetMeshColorRevision(const Entity& entity) const {

	const uint64_t key = (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
	const auto found = meshColorRevisions_.find(key);
	return found == meshColorRevisions_.end() ? 0 : found->second;
}

void ECSChangeTracker::MarkTransformConsumersModified(ComponentChangeChannel channels, std::span<const Entity> changedTransforms) {

	if (HasComponentChangeChannel(
		channels, ComponentChangeChannel::Render)) {
		IncrementRevision(renderTransformRevision_);

		RenderTransformChangeBatch batch{};
		batch.revision = renderTransformRevision_;
		batch.entities.assign(
			changedTransforms.begin(), changedTransforms.end());
		renderTransformChangeHistory_.emplace_back(std::move(batch));
		while (kRenderTransformHistoryCount <
			renderTransformChangeHistory_.size()) {
			renderTransformChangeHistory_.pop_front();
		}
	}
	if (HasComponentChangeChannel(
		channels, ComponentChangeChannel::Lighting)) {
		IncrementRevision(lightDataRevision_);
	}
}

bool ECSChangeTracker::CollectRenderTransformChanges(uint64_t afterRevision, std::vector<Entity>& outEntities) const {

	outEntities.clear();
	if (afterRevision == renderTransformRevision_) {
		return true;
	}
	if (renderTransformRevision_ < afterRevision) {
		return false;
	}
	if (renderTransformChangeHistory_.empty() ||
		afterRevision + 1 <
		renderTransformChangeHistory_.front().revision) {
		return false;
	}

	for (const RenderTransformChangeBatch& batch :
		renderTransformChangeHistory_) {
		if (batch.revision <= afterRevision) {
			continue;
		}
		outEntities.insert(outEntities.end(),
			batch.entities.begin(), batch.entities.end());
	}
	std::sort(outEntities.begin(), outEntities.end(),
		[](const Entity& lhs, const Entity& rhs) {
			if (lhs.index != rhs.index) {
				return lhs.index < rhs.index;
			}
			return lhs.generation < rhs.generation;
		});
	outEntities.erase(
		std::unique(outEntities.begin(), outEntities.end()),
		outEntities.end());
	return true;
}

uint64_t ECSChangeTracker::AddComponentMutationListener(ComponentMutationCallback callback, void* userData) {

	if (!callback) {
		return 0;
	}

	const uint64_t listenerID = nextComponentMutationListenerID_++;
	componentMutationListeners_.emplace_back(ComponentMutationListener{
		.id = listenerID,
		.callback = callback,
		.userData = userData,
		});
	return listenerID;
}

void ECSChangeTracker::RemoveComponentMutationListener(uint64_t listenerID) {

	if (listenerID == 0) {
		return;
	}
	componentMutationListeners_.erase(
		std::remove_if(componentMutationListeners_.begin(), componentMutationListeners_.end(),
			[listenerID](const ComponentMutationListener& listener) {
				return listener.id == listenerID;
			}),
		componentMutationListeners_.end());
}

void ECSChangeTracker::IncrementRevision(uint64_t& revision) {

	++revision;
	if (revision == 0) {
		revision = 1;
	}
}

void ECSChangeTracker::Notify(ECSWorld& world, const Entity& entity, uint32_t typeID,
	ComponentMutationKind kind, ComponentChangeChannel channels) {

	if (HasComponentChangeChannel(
		channels, ComponentChangeChannel::Render)) {
		MarkRenderDataModified(entity);
	}
	if (HasComponentChangeChannel(
		channels, ComponentChangeChannel::Lighting)) {
		IncrementRevision(lightDataRevision_);
	}
	// 破棄通知で作られた世代記録もここで回収する
	if (kind == ComponentMutationKind::EntityDestroyed) {
		const uint64_t key = (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
		meshColorRevisions_.erase(key);
		entityRenderRevisions_.erase(key);
	}
	// 購読の追加削除はWorldEnter/Exitだけで行い、通知中の割り当てを避ける
	for (const ComponentMutationListener& listener : componentMutationListeners_) {
		if (listener.callback) {
			listener.callback(world, entity, typeID, kind, listener.userData);
		}
	}
}

void ECSChangeTracker::MarkLightDataModified() {

	IncrementRevision(lightDataRevision_);
}

void ECSChangeTracker::CopySerializationRevisionsFrom(const ECSChangeTracker& source) {

	dataRevision_ = source.dataRevision_;
	renderDataRevision_ = source.renderDataRevision_;
	renderTransformRevision_ = source.renderTransformRevision_;
	lightDataRevision_ = source.lightDataRevision_;
}
