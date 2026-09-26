#include "ECSChangeTracker.h"

//============================================================================
//	include
//============================================================================

// c++
#include <algorithm>
#include <exception>
#include <stdexcept>

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

	if (nextComponentMutationListenerID_ == UINT64_MAX) {
		throw std::overflow_error("Component通知の購読IDが上限に達しました");
	}
	const uint64_t listenerID = nextComponentMutationListenerID_;
	componentMutationListeners_.emplace_back(ComponentMutationListener{
		.id = listenerID,
		.callback = callback,
		.userData = userData,
		});
	++nextComponentMutationListenerID_;
	return listenerID;
}

void ECSChangeTracker::RemoveComponentMutationListener(uint64_t listenerID) {

	if (listenerID == 0) {
		return;
	}
	for (auto& listener : componentMutationListeners_) {
		if (listener.id == listenerID) {
			listener.callback = nullptr;
			listener.userData = nullptr;
			break;
		}
	}
	if (notificationDepth_ == 0) {
		std::erase_if(componentMutationListeners_, [](const auto& listener) { return !listener.callback; });
	}
}

void ECSChangeTracker::IncrementRevision(uint64_t& revision) {

	if (revision == UINT64_MAX) {
		throw std::overflow_error("Worldの変更世代が上限に達しました");
	}
	++revision;
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
	// 通知開始時の件数までを対象にし、削除済み購読は呼ばない
	const size_t count = componentMutationListeners_.size();
	++notificationDepth_;
	std::exception_ptr failure;
	for (size_t index = 0; index < count; ++index) {
		const ComponentMutationListener listener = componentMutationListeners_[index];
		if (!listener.callback) {
			continue;
		}
		try {
			listener.callback(world, entity, typeID, kind, listener.userData);
		} catch (...) {

			// 内部キャッシュへの通知を済ませてから失敗を返す
			if (!failure) {
				failure = std::current_exception();
			}
		}
	}
	--notificationDepth_;
	if (notificationDepth_ == 0) {
		std::erase_if(componentMutationListeners_, [](const auto& listener) { return !listener.callback; });
	}
	if (failure) {
		std::rethrow_exception(failure);
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
