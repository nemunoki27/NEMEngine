#include "TestContracts.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>

// c++
#include <array>
#include <stdexcept>

namespace {

	// 構築と複製の途中失敗を発生させるComponent
	struct ThrowingComponent {

		static inline bool failConstruct = false;
		static inline bool failRelease = false;
		static inline int releaseCount = 0;
		static inline int copiesBeforeThrow = -1;
		static inline int liveCount = 0;
		int value = 7;

		ThrowingComponent();
		ThrowingComponent(const ThrowingComponent& source);
		ThrowingComponent(ThrowingComponent&& source) noexcept;
		~ThrowingComponent();
		ThrowingComponent& operator=(const ThrowingComponent&) = default;
		ThrowingComponent& operator=(ThrowingComponent&&) = default;
	};

	ThrowingComponent::ThrowingComponent() {

		if (failConstruct) {
			throw std::runtime_error("Component構築失敗の検証");
		}
		++liveCount;
	}

	ThrowingComponent::ThrowingComponent(const ThrowingComponent& source) : value(source.value) {

		if (copiesBeforeThrow == 0) {
			throw std::runtime_error("Component複製失敗の検証");
		}
		if (copiesBeforeThrow > 0) {
			--copiesBeforeThrow;
		}
		++liveCount;
	}

	ThrowingComponent::ThrowingComponent(ThrowingComponent&& source) noexcept : value(source.value) {

		++liveCount;
		source.value = 0;
	}

	ThrowingComponent::~ThrowingComponent() {

		--liveCount;
	}

	void to_json(nlohmann::json& out, const ThrowingComponent& value) {

		out = value.value;
	}

	void from_json(const nlohmann::json& in, ThrowingComponent& value) {

		value.value = in.get<int>();
	}

	void ReleaseComponentStorage(Engine::ECSWorld&, const Engine::Entity&, ThrowingComponent&) {

		++ThrowingComponent::releaseCount;
		if (ThrowingComponent::failRelease) {
			throw std::runtime_error("Component解放失敗の検証");
		}
	}

	struct QueryComponent {

		static constexpr bool kEnableable = true;
		int value = 0;
	};

	// 追加通知の失敗と通知中の自己破棄を発生させる
	struct FailingAddedComponent {

		static constexpr bool kSerializable = false;
		static constexpr bool kHasECSHooks = true;
		static inline bool destroyOwner = false;
		static void InitializeStorage(Engine::ECSWorld&, const Engine::Entity&, FailingAddedComponent&) {}
		static void ReleaseStorage(Engine::ECSWorld&, const Engine::Entity&, FailingAddedComponent&) {}
		static void OnAdded(Engine::ECSWorld& world, const Engine::Entity& entity, FailingAddedComponent&);
	};

	void FailingAddedComponent::OnAdded(Engine::ECSWorld& world, const Engine::Entity& entity, FailingAddedComponent&) {

		if (destroyOwner) {
			world.DestroyEntity(entity);
			world.FlushPendingDestroyEntities();
			return;
		}
		throw std::runtime_error("追加通知失敗の検証");
	}

	void to_json(nlohmann::json& out, const QueryComponent& value) {

		out = value.value;
	}

	void from_json(const nlohmann::json& in, QueryComponent& value) {

		value.value = in.get<int>();
	}

	struct QueryTag {

		static constexpr Engine::ComponentStorageKind kStorageKind = Engine::ComponentStorageKind::Tag;
	};

	// 1行だけでもChunkへ収まらないComponent
	struct OversizedComponent {

		static constexpr bool kSerializable = false;
		std::array<std::byte, Engine::kChunkBytes + 1> bytes{};
	};

	struct PendingBufferElement {

		static constexpr bool kSerializable = false;
		static constexpr Engine::ComponentStorageKind kStorageKind = Engine::ComponentStorageKind::Buffer;
		static constexpr uint32_t kInternalBufferCapacity = 2;
		int32_t value = 0;
	};

	void RegisterStructureComponents() {

		[[maybe_unused]] static const bool registered = [] {
			auto& registry = Engine::ComponentTypeRegistry::GetInstance();
			registry.Register<ThrowingComponent>(registry.GetComponentTypeCount(), "ThrowingComponent");
			registry.Register<OversizedComponent>(registry.GetComponentTypeCount(), "OversizedComponent");
			registry.Register<PendingBufferElement>(registry.GetComponentTypeCount(), "PendingBufferElement");
			registry.Register<QueryComponent>(registry.GetComponentTypeCount(), "QueryComponent");
			registry.Register<QueryTag>(registry.GetComponentTypeCount(), "QueryTag");
			registry.Register<FailingAddedComponent>(registry.GetComponentTypeCount(), "FailingAddedComponent");
			return true;
		}();
	}

	// 部分構築、構造移動、複製失敗から元のWorldを保護する
	bool CheckStructureFailure() {

		Engine::ECSWorld world;
		auto& registry = Engine::ComponentTypeRegistry::GetInstance();
		const uint32_t typeID = registry.GetID<ThrowingComponent>();
		const auto stable = Engine::UUID::New();
		const auto original = world.CreateEntity(stable);
		const std::array<uint32_t, 1> failingTypes{ registry.GetID<FailingAddedComponent>() };
		for (bool destroy : { false, true }) {
			FailingAddedComponent::destroyOwner = destroy;
			bool rejected = false;
			try {
				world.CreateEntityWithComponents(failingTypes, stable);
			} catch (const std::runtime_error&) {
				rejected = true;
			}
			if (!rejected || !world.IsAlive(original) || world.FindByUUID(stable) != original) {
				return false;
			}
		}
		const std::array<uint32_t, 1> types{ typeID };
		ThrowingComponent::failConstruct = true;
		bool rejectedCreate = false;
		try {
			world.CreateEntityWithComponents(types);
		} catch (const std::runtime_error&) {
			rejectedCreate = true;
		}
		const Engine::Entity first = world.CreateEntity();
		world.AddComponent<Engine::NameComponent>(first).name = "Preserved";
		const uint32_t nameType = registry.GetID<Engine::NameComponent>();
		const uint64_t nameInstance = world.GetComponentInstanceID(first, nameType);
		bool rejectedAdd = false;
		try {
			world.AddComponent<ThrowingComponent>(first);
		} catch (const std::runtime_error&) {
			rejectedAdd = true;
		}
		ThrowingComponent::failConstruct = false;
		if (!rejectedCreate || !rejectedAdd || ThrowingComponent::liveCount != 0 ||
			world.GetComponentInstanceID(first, nameType) != nameInstance || world.HasComponent<ThrowingComponent>(first) ||
			world.GetComponent<Engine::NameComponent>(first).name != "Preserved") {
			return false;
		}

		world.AddComponent<ThrowingComponent>(first).value = 31;
		const uint64_t firstInstance = world.GetComponentInstanceID(first, typeID);
		world.AddComponent<Engine::SceneObjectComponent>(first);
		if (world.GetComponentInstanceID(first, typeID) != firstInstance || world.GetComponent<ThrowingComponent>(first).value != 31) {
			return false;
		}
		world.RemoveComponent<ThrowingComponent>(first);
		world.AddComponent<ThrowingComponent>(first).value = 37;
		if (world.GetComponentInstanceID(first, typeID) == firstInstance) {
			return false;
		}
		const auto second = world.CreateEntityWithComponents(types);
		world.GetComponent<ThrowingComponent>(second).value = 41;

		// 1行の複製後に失敗させ、候補だけが破棄されることを確認する
		ThrowingComponent::copiesBeforeThrow = 1;
		bool rejectedClone = false;
		try {
			world.CloneForSerialization();
		} catch (const std::runtime_error&) {
			rejectedClone = true;
		}
		ThrowingComponent::copiesBeforeThrow = -1;
		if (!rejectedClone || ThrowingComponent::liveCount != 2 || world.GetComponent<ThrowingComponent>(first).value != 37 ||
			world.GetComponent<ThrowingComponent>(second).value != 41) {
			return false;
		}

		bool oversized = false;
		try {
			world.AddComponent<OversizedComponent>(first);
		} catch (const std::length_error&) {
			oversized = true;
		}
		return oversized && !world.HasComponent<OversizedComponent>(first) && world.IsAlive(world.CreateEntity());
	}

	// 走査中は行を維持し、安全地点で追加と削除を適用する
	bool CheckQueryMutation() {

		Engine::ECSWorld world;
		const auto first = world.CreateEntity();
		const auto second = world.CreateEntity();
		world.AddComponent<ThrowingComponent>(first);
		world.AddComponent<ThrowingComponent>(second);
		uint32_t visited = 0;
		bool rejected = false;
		world.ForEach<ThrowingComponent>([&](const Engine::Entity& entity, ThrowingComponent& value) {
			++visited;
			try {
				world.AddComponent<Engine::NameComponent>(entity);
			} catch (const std::logic_error&) {
				rejected = true;
			}
			world.GetCommandBuffer().EnqueueAddComponentByName(entity, "Name");
			world.GetCommandBuffer().Flush(world);
			world.DestroyEntity(first);
			world.FlushPendingDestroyEntities();
			value.value = 53;
		});
		if (!rejected || visited != 2 || !world.IsAlive(first) || world.HasComponent<Engine::NameComponent>(second)) {
			return false;
		}
		world.GetCommandBuffer().Flush(world);
		world.FlushPendingDestroyEntities();
		if (world.IsAlive(first) || !world.HasComponent<Engine::NameComponent>(second) ||
			world.GetComponent<ThrowingComponent>(second).value != 53) {
			return false;
		}

		// Entity一覧の走査は新規追加分へ際限なく広がらない
		visited = 0;
		world.ForEachAliveEntity([&](const Engine::Entity&) {
			++visited;
			world.CreateEntity();
		});
		return visited == 1;
	}

	// 追加前の値、取消、失敗後の残りのCommandを確認する
	bool CheckPendingComponents() {

		Engine::ECSWorld world;
		auto& commands = world.GetCommandBuffer();
		const uint32_t typeID = Engine::ComponentTypeRegistry::GetInstance().GetID<ThrowingComponent>();
		const auto entity = world.CreateEntity();
		const uint64_t pendingID = commands.StageAddComponent(world, entity, typeID);
		if (pendingID == 0 || pendingID != commands.StageAddComponent(world, entity, typeID) ||
			world.HasComponent<ThrowingComponent>(entity) || world.GetBindingComponentInstanceID(entity, typeID) != pendingID) {
			return false;
		}
		world.TryGetComponentForBinding<ThrowingComponent>(entity)->value = 93;
		commands.Flush(world);
		if (world.GetComponent<ThrowingComponent>(entity).value != 93 ||
			world.GetComponentInstanceID(entity, typeID) != pendingID || !commands.IsEmpty()) {
			return false;
		}

		// 取消後に同じ型を予約しても古い参照を復活させない
		world.RemoveComponent<ThrowingComponent>(entity);
		const uint64_t cancelledID = commands.StageAddComponent(world, entity, typeID);
		commands.Clear();
		if (world.TryGetComponentForBinding<ThrowingComponent>(entity) || ThrowingComponent::liveCount != 0) {
			return false;
		}
		const uint64_t retryID = commands.StageAddComponent(world, entity, typeID);
		if (retryID == pendingID || retryID == cancelledID) {
			return false;
		}
		commands.EnqueueSetNameEnsuringComponent(entity, "AfterFailure");
		ThrowingComponent::copiesBeforeThrow = 0;
		bool failed = false;
		try {
			commands.Flush(world);
		} catch (const std::runtime_error&) {
			failed = true;
		}
		ThrowingComponent::copiesBeforeThrow = -1;
		if (!failed || commands.IsEmpty() || ThrowingComponent::liveCount != 0 ||
			world.GetBindingComponentInstanceID(entity, typeID) != 0) {
			return false;
		}
		commands.Flush(world);
		if (world.GetComponent<Engine::NameComponent>(entity).name != "AfterFailure") {
			return false;
		}

		// 追加、削除、再追加を同じ安全地点までに予約する
		const uint64_t removedID = commands.StageAddComponent(world, entity, typeID);
		world.TryGetComponentForBinding<ThrowingComponent>(entity)->value = 121;
		commands.EnqueueRemoveComponentByName(entity, "ThrowingComponent");
		if (world.GetBindingComponentInstanceID(entity, typeID) != 0 || ThrowingComponent::liveCount != 0) {
			return false;
		}
		const uint64_t readdedID = commands.StageAddComponent(world, entity, typeID);
		commands.Flush(world);
		if (readdedID == removedID || world.GetComponentInstanceID(entity, typeID) != readdedID ||
			world.GetComponent<ThrowingComponent>(entity).value != 7) {
			return false;
		}
		world.RemoveComponent<ThrowingComponent>(entity);

		// 予約後にEntityが失効しても次の世代へ値を渡さない
		commands.StageAddComponent(world, entity, typeID);
		world.DestroyEntity(entity);
		world.FlushPendingDestroyEntities();
		const auto replacement = world.CreateEntity();
		commands.Flush(world);
		return replacement.index == entity.index && replacement.generation != entity.generation &&
			!world.HasComponent<ThrowingComponent>(replacement) && ThrowingComponent::liveCount == 0;
	}

	// 追加前の可変長配列も外部領域ごと安全に引き継ぐ
	bool CheckPendingBuffer() {

		Engine::ECSWorld world;
		const auto entity = world.CreateEntity();
		const uint32_t typeID = Engine::ComponentTypeRegistry::GetInstance().GetID<PendingBufferElement>();
		const uint64_t instanceID = world.GetCommandBuffer().StageAddComponent(world, entity, typeID);
		const std::array<PendingBufferElement, 4> values{ { { 13 }, { 29 }, { 41 }, { 57 } } };
		if (!world.TryGetBufferForBinding(entity, typeID).SetData(values.data(), static_cast<uint32_t>(values.size()))) {
			return false;
		}
		world.FlushWorldCommands();
		auto buffer = world.TryGetUntypedBuffer(entity, typeID);
		std::array<PendingBufferElement, 4> copied{};
		if (buffer.CopyTo(copied.data(), static_cast<uint32_t>(copied.size())) != values.size() ||
			copied[0].value != 13 || copied[3].value != 57 || world.GetComponentInstanceID(entity, typeID) != instanceID) {
			return false;
		}
		// 自分の部分列を詰め、範囲外の指定では内容を維持する
		auto* source = static_cast<PendingBufferElement*>(buffer.GetData());
		if (buffer.SetData(source + 3, 2) || !buffer.SetData(source + 1, 3)) {
			return false;
		}
		if (buffer.CopyTo(copied.data(), 4) != 3 || copied[0].value != 29 || copied[2].value != 57) {
			return false;
		}
		// 右へ重なるコピーでも元の順序を維持する
		source = static_cast<PendingBufferElement*>(buffer.GetData());
		return buffer.CopyTo(source + 1, 2) == 2 && source[1].value == 29 && source[2].value == 41;
	}

	// 型番号の走査条件と値を持たないTagの移動を確認する
	bool CheckQueryModes() {

		Engine::ECSWorld world;
		const auto entity = world.CreateEntity();
		world.AddComponent<QueryComponent>(entity).value = 19;
		world.AddComponentByName(entity, "QueryTag");
		world.SetComponentEnabled<QueryComponent>(entity, false);
		const uint32_t typeID = Engine::ComponentTypeRegistry::GetInstance().GetID<QueryComponent>();
		uint32_t enabledCount = 0;
		uint32_t allCount = 0;
		world.ForEach(typeID, [&](const Engine::Entity&) { ++enabledCount; });
		world.ForEach(typeID, [&](const Engine::Entity&) { ++allCount; }, Engine::ECSQueryMode::IncludeDisabled);
		const auto snapshot = world.CloneForSerialization();
		if (enabledCount != 0 || allCount != 1 || !snapshot->HasComponent<QueryTag>(entity) ||
			snapshot->IsComponentEnabled<QueryComponent>(entity) || snapshot->GetComponent<QueryComponent>(entity).value != 19) {
			return false;
		}
		world.RemoveComponentByName(entity, "QueryTag");
		Engine::EntitySignature invalid{};
		invalid.Set(Engine::kMaxComponentTypes - 1);
		bool rejected = false;
		try {
			world.CreateEntityWithSignature(invalid);
		} catch (const std::invalid_argument&) {
			rejected = true;
		}
		return rejected && world.GetComponent<QueryComponent>(entity).value == 19;
	}

	// 解放処理の例外でも残りのEntityを終了する
	bool CheckWorldReleaseFailure() {

		ThrowingComponent::releaseCount = 0;
		{
			Engine::ECSWorld world;
			world.AddComponent<ThrowingComponent>(world.CreateEntity());
			world.AddComponent<ThrowingComponent>(world.CreateEntity());
			ThrowingComponent::failRelease = true;
		}
		ThrowingComponent::failRelease = false;
		return ThrowingComponent::releaseCount == 2 && ThrowingComponent::liveCount == 0;
	}

	struct NotificationState {

		uint64_t removedListener = 0;
		bool entered = false;
		uint32_t removedCalls = 0;
	};

	void RemovedListener(Engine::ECSWorld&, const Engine::Entity&, uint32_t,
		Engine::ComponentMutationKind, void* data) {

		++static_cast<NotificationState*>(data)->removedCalls;
	}

	void ReentrantListener(Engine::ECSWorld& world, const Engine::Entity& entity, uint32_t,
		Engine::ComponentMutationKind kind, void* data) {

		auto& state = *static_cast<NotificationState*>(data);
		if (state.entered || kind != Engine::ComponentMutationKind::Added) {
			return;
		}
		state.entered = true;
		world.RemoveComponentMutationListener(state.removedListener);
		for (uint32_t index = 0; index < 64; ++index) {
			world.AddComponentMutationListener(RemovedListener, data);
		}
		world.AddComponent<Engine::NameComponent>(entity).name = "Reentrant";
	}

	// 通知中の購読変更と構造移動後も正しい参照を返す
	bool CheckNotificationMutation() {

		NotificationState state;
		Engine::ECSWorld world;
		const auto entity = world.CreateEntity();
		world.AddComponentMutationListener(ReentrantListener, &state);
		state.removedListener = world.AddComponentMutationListener(RemovedListener, &state);
		ThrowingComponent& result = world.AddComponent<ThrowingComponent>(entity);
		return &result == world.TryGetComponent<ThrowingComponent>(entity) && result.value == 7 &&
			world.GetComponent<Engine::NameComponent>(entity).name == "Reentrant" && state.removedCalls == 64;
	}
	bool CheckPendingGameObject() {

		Engine::ECSWorld world;
		const auto entity = world.CreateEntity();
		world.GetCommandBuffer().EnqueueCreateEntity(world, entity, "Pending", Engine::Entity::Null());
		auto* transform = world.TryGetComponentForBinding<Engine::TransformComponent>(entity);
		const auto typeID = Engine::ComponentTypeRegistry::GetInstance().GetID<Engine::TransformComponent>();
		const auto instanceID = world.GetBindingComponentInstanceID(entity, typeID);
		if (!transform || !instanceID || world.HasComponent<Engine::TransformComponent>(entity)) {
			return false;
		}

		// 生成直後の値を読み戻し、通常走査へはまだ公開しない
		transform->localPos = Engine::Vector3(13.0f, 17.0f, 19.0f);
		world.TryGetComponentForBinding<Engine::NameComponent>(entity)->name = "Edited";
		Engine::ResolvedWorldTransform resolved{};
		if (Engine::TransformWorldUtility::ResolveWorldTransform(world, entity, resolved) ||
			!Engine::TransformWorldUtility::ResolveWorldTransform(world, entity, resolved, true) ||
			resolved.matrix.GetTranslationValue().x != 13.0f) {
			return false;
		}

		// 実体化でも個体番号と編集値を引き継ぐ
		world.GetCommandBuffer().Flush(world);
		return world.GetComponentInstanceID(entity, typeID) == instanceID &&
			world.GetComponent<Engine::TransformComponent>(entity).localPos.y == 17.0f &&
			world.GetComponent<Engine::NameComponent>(entity).name == "Edited" &&
			static_cast<bool>(world.GetComponent<Engine::SceneObjectComponent>(entity).localFileID);
	}
}

bool NEMTests::TestECSStructureSafety() {

	RegisterStructureComponents();
	return CheckStructureFailure() && ThrowingComponent::liveCount == 0 && CheckQueryMutation() &&
		ThrowingComponent::liveCount == 0 && CheckNotificationMutation() && ThrowingComponent::liveCount == 0 &&
		CheckPendingComponents() && ThrowingComponent::liveCount == 0 && CheckPendingBuffer() && CheckPendingGameObject() && CheckQueryModes() && CheckWorldReleaseFailure();
}
