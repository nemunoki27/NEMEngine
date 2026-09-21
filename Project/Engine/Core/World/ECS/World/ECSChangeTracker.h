#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Core/ComponentType.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>

// c++
#include <deque>
#include <span>
#include <unordered_map>
#include <vector>

namespace Engine {

	class ECSWorld;

	//============================================================================
	//	ComponentMutationKind enum
	//============================================================================
	enum class ComponentMutationKind : uint8_t {

		Added,
		Removed,
		Modified,
		EntityDestroyed,
	};

	//============================================================================
	//	ECSChangeTracker class
	//	ワールドの変更世代と購読通知を管理する
	//============================================================================
	class ECSChangeTracker {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		using ComponentMutationCallback = void(*)(
			ECSWorld&, const Entity&, uint32_t, ComponentMutationKind, void*);

		// ワールドの変更世代を進める
		void MarkDataModified();
		// 描画構成の変更を記録する
		void MarkRenderDataModified();
		// 描画構成の変更を記録する
		void MarkRenderDataModified(const Entity& entity);
		// 色だけの変更を個別に記録する
		void MarkMeshColorModified(const Entity& entity);
		uint64_t GetEntityRenderRevision(const Entity& entity) const;
		uint64_t GetMeshColorRevision(const Entity& entity) const;
		// Transformの変更先と差分を記録する
		void MarkTransformConsumersModified(ComponentChangeChannel channels, std::span<const Entity> changedTransforms);
		// 指定世代以降のTransform差分を収集する
		bool CollectRenderTransformChanges(uint64_t afterRevision, std::vector<Entity>& outEntities) const;
		// 変更通知の購読を追加する
		uint64_t AddComponentMutationListener(ComponentMutationCallback callback, void* userData);
		// 変更通知の購読を解除する
		void RemoveComponentMutationListener(uint64_t listenerID);
		// 描画側の照明更新世代を進める
		void MarkLightDataModified();
		// 変更先の世代を更新して購読先へ通知する
		void Notify(ECSWorld& world, const Entity& entity, uint32_t typeID,
			ComponentMutationKind kind, ComponentChangeChannel channels);
		// 保存用複製に必要な世代だけを引き継ぐ
		void CopySerializationRevisionsFrom(const ECSChangeTracker& source);

		//--------- accessor -----------------------------------------------------

		uint64_t GetDataRevision() const { return dataRevision_; }
		uint64_t GetRenderDataRevision() const { return renderDataRevision_; }
		uint64_t GetRenderTransformRevision() const { return renderTransformRevision_; }
		uint64_t GetLightDataRevision() const { return lightDataRevision_; }
		uint64_t GetMeshColorRevision() const { return meshColorRevision_; }
		uint64_t GetRenderResetRevision() const { return renderResetRevision_; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// Component変更通知の購読情報
		struct ComponentMutationListener {

			uint64_t id = 0;
			ComponentMutationCallback callback = nullptr;
			void* userData = nullptr;
		};
		// 描画Transformの世代ごとの差分
		struct RenderTransformChangeBatch {

			uint64_t revision = 0;
			std::vector<Entity> entities;
		};

		//--------- variables ----------------------------------------------------

		static constexpr size_t kRenderTransformHistoryCount = 8;

		std::vector<ComponentMutationListener> componentMutationListeners_;
		std::deque<RenderTransformChangeBatch>
			renderTransformChangeHistory_;
		uint64_t nextComponentMutationListenerID_ = 1;
		// 0を未構築値として扱えるよう1から開始する
		uint64_t dataRevision_ = 1;
		uint64_t renderDataRevision_ = 1;
		uint64_t renderResetRevision_ = 1;
		uint64_t meshColorRevision_ = 1;
		// Entityの世代を含むキーで再利用後の変更を区別する
		std::unordered_map<uint64_t, uint64_t> entityRenderRevisions_;
		std::unordered_map<uint64_t, uint64_t> meshColorRevisions_;
		uint64_t renderTransformRevision_ = 1;
		uint64_t lightDataRevision_ = 1;

		//--------- functions ----------------------------------------------------

		// 0を飛ばして変更世代を進める
		static void IncrementRevision(uint64_t& revision);
	};
} // Engine
