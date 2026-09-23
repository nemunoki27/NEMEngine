#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Core/ComponentType.h>
#include <Engine/Core/World/ECS/Components/Core/DynamicBuffer.h>
#include <Engine/Core/World/ECS/Config/ECSConfig.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>

// c++
#include <string>
#include <vector>
#include <unordered_map>
#include <cstring>
#include <type_traits>

namespace Engine {

	namespace ComponentTypeTraits {

		template <typename T>
		consteval ComponentStorageKind ResolveStorageKind() {

			if constexpr (requires { T::kStorageKind; }) {
				return T::kStorageKind;
			}
			return ComponentStorageKind::Data;
		}

		template <typename T>
		consteval ComponentWorldDomain ResolveWorldDomain() {

			if constexpr (requires { T::kWorldDomain; }) {
				return T::kWorldDomain;
			}
			return ComponentWorldDomain::Both;
		}

		template <typename T>
		consteval bool ResolveEnableable() {

			if constexpr (requires { T::kEnableable; }) {
				return T::kEnableable;
			}
			return false;
		}

		template <typename T>
		consteval bool ResolveECSHooks() {

			if constexpr (requires { T::kHasECSHooks; }) {
				return T::kHasECSHooks;
			}
			return false;
		}

		template <typename T>
		consteval bool ResolveSerializable() {

			if constexpr (requires { T::kSerializable; }) {
				return T::kSerializable;
			}
			return true;
		}

		template <typename T>
		consteval ComponentChangeChannel ResolveChangeChannels() {

			if constexpr (requires { T::kChangeChannels; }) {
				return T::kChangeChannels;
			}
			return ComponentChangeChannel::None;
		}

		template <typename T>
		consteval ComponentChangeChannel ResolveTransformChannels() {

			if constexpr (requires { T::kTransformChannels; }) {
				return T::kTransformChannels;
			}
			return ComponentChangeChannel::None;
		}

		template <typename T>
		consteval uint32_t ResolveInternalBufferCapacity() {

			if constexpr (requires { T::kInternalBufferCapacity; }) {
				return T::kInternalBufferCapacity;
			}
			constexpr size_t kDefaultInlineBytes = 128;
			return static_cast<uint32_t>((std::max)(size_t{ 1 }, kDefaultInlineBytes / sizeof(T)));
		}
	}

	//============================================================================
	//	ComponentTypeRegistry class
	//	コンポーネントの種類を管理するクラス
	//============================================================================
	class ComponentTypeRegistry {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ComponentTypeRegistry();
		~ComponentTypeRegistry() = default;

		// Manifestで指定された固定IDへコンポーネントの種類を登録
		template <typename T>
		void Register(uint32_t id, const std::string_view& name);
		//--------- accessor -----------------------------------------------------

		// 登録されているコンポーネント種類の数を返す
		uint32_t GetComponentTypeCount() const { return static_cast<uint32_t>(infos_.size()); }

		// コンポーネントの種類IDを返す
		template <typename T>
		uint32_t GetID() const;

		const ComponentTypeInfo& GetInfo(uint32_t id) const;
		const ComponentTypeInfo* FindByName(const std::string_view& name) const;

		// シングルトン
		static ComponentTypeRegistry& GetInstance();
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		std::vector<ComponentTypeInfo> infos_;
		std::unordered_map<std::string, uint32_t> nameToID_;
		std::unordered_map<const void*, uint32_t> typeKeyToID_;

		template <typename T>
		static const void* GetTypeKey();
	};

	//============================================================================
	//	ComponentTypeRegistry templateMethods
	//============================================================================
	template <typename T>
	inline void ComponentTypeRegistry::Register(uint32_t id, const std::string_view& name) {

		Assert::Call(id == GetComponentTypeCount(), "ComponentManifestのIDは0から連続させてください");
		Assert::Call(id < kMaxComponentTypes, "kMaxComponentTypesを増やしてください");
		Assert::Call(!nameToID_.contains(std::string(name)), "同名のComponentTypeが既に登録されています");
		Assert::Call(!typeKeyToID_.contains(GetTypeKey<T>()), "同じC++型が既に登録されています");

		// コンポーネントの情報を作成
		ComponentTypeInfo info{};
		info.name = std::string(name);
		info.id = id;
		info.storageKind = ComponentTypeTraits::ResolveStorageKind<T>();
		info.worldDomain = ComponentTypeTraits::ResolveWorldDomain<T>();
		info.enableable = ComponentTypeTraits::ResolveEnableable<T>();
		info.serializable = ComponentTypeTraits::ResolveSerializable<T>();
		info.changeChannels =
			ComponentTypeTraits::ResolveChangeChannels<T>();
		info.transformChannels =
			ComponentTypeTraits::ResolveTransformChannels<T>();

		if constexpr (
			ComponentTypeTraits::ResolveStorageKind<T>() == ComponentStorageKind::Buffer) {

			info.internalBufferCapacity =
				ComponentTypeTraits::ResolveInternalBufferCapacity<T>();
			info.elementSize = sizeof(T);
			info.elementAlign = alignof(T);
			info.bufferElementTriviallyCopyable =
				std::is_trivially_copyable_v<T>;
			const size_t inlineOffset =
				(sizeof(DynamicBufferHeader) + alignof(T) - 1) & ~(alignof(T) - 1);
			info.size = inlineOffset + sizeof(T) * info.internalBufferCapacity;
			info.align = (std::max)(alignof(DynamicBufferHeader), alignof(T));
			info.triviallyRelocatable = false;
			info.triviallyDestructible = false;
			info.nothrowMoveConstructible = true;
		} else {

			info.size = info.storageKind == ComponentStorageKind::Tag ? 0 : sizeof(T);
			info.align = alignof(T);
			info.triviallyRelocatable = std::is_trivially_copyable_v<T>;
			info.triviallyDestructible = std::is_trivially_destructible_v<T>;
			info.nothrowMoveConstructible = std::is_nothrow_move_constructible_v<T>;
		}

		// 型Tの関数を登録
		if constexpr (
			ComponentTypeTraits::ResolveStorageKind<T>() == ComponentStorageKind::Buffer) {

			info.constructDefault = [](void* ptr) {
				DynamicBufferStorage::Construct<T>(
					ptr, ComponentTypeTraits::ResolveInternalBufferCapacity<T>());
				};
		} else {

			info.constructDefault = [](void* ptr) { new (ptr) T(); };
		}
		info.initializeStorage = []([[maybe_unused]] ECSWorld& world,
			[[maybe_unused]] const Entity& entity, [[maybe_unused]] void* ptr) {

			if constexpr (ComponentTypeTraits::ResolveStorageKind<T>() != ComponentStorageKind::Buffer) {

				T& component = *static_cast<T*>(ptr);
				if constexpr (ComponentTypeTraits::ResolveECSHooks<T>()) {
					T::InitializeStorage(world, entity, component);
				} else if constexpr (requires(
					ECSWorld& hookWorld, const Entity& hookEntity, T& hookComponent) {
						InitializeComponentStorage(
							hookWorld, hookEntity, hookComponent);
					}) {
					InitializeComponentStorage(world, entity, component);
				}
			}
			};
		info.onAdded = []([[maybe_unused]] ECSWorld& world,
			[[maybe_unused]] const Entity& entity, [[maybe_unused]] void* ptr) {

			if constexpr (ComponentTypeTraits::ResolveECSHooks<T>()) {

				T& component = *static_cast<T*>(ptr);
				T::OnAdded(world, entity, component);
			}
			};
		info.onRemoved = []([[maybe_unused]] ECSWorld& world,
			[[maybe_unused]] const Entity& entity) {

			if constexpr (ComponentTypeTraits::ResolveECSHooks<T>() &&
				requires(ECSWorld& hookWorld, const Entity& hookEntity) {
					T::OnRemoved(hookWorld, hookEntity);
				}) {
				T::OnRemoved(world, entity);
			}
			};
		if constexpr (
			ComponentTypeTraits::ResolveStorageKind<T>() == ComponentStorageKind::Buffer) {
			info.destroy = [](void* ptr) { DynamicBufferStorage::Destroy<T>(ptr); };
		} else if constexpr (std::is_trivially_destructible_v<T>) {
			info.destroy = [](void*) {};
		} else {
			info.destroy = [](void* ptr) { ((T*)ptr)->~T(); };
		}
		if constexpr (
			ComponentTypeTraits::ResolveStorageKind<T>() == ComponentStorageKind::Buffer) {
			info.copyConstruct = [](void* dst, const void* src) {
				DynamicBufferStorage::Copy<T>(dst, src);
				};
			info.moveConstruct = [](void* dst, void* src) {
				DynamicBufferStorage::Move<T>(dst, src);
				};
		} else if constexpr (std::is_trivially_copyable_v<T>) {
			info.copyConstruct = [](void* dst, const void* src) {
				std::memcpy(dst, src, sizeof(T));
				};
			info.moveConstruct = [](void* dst, void* src) { std::memcpy(dst, src, sizeof(T)); };
		} else {
			static_assert(std::is_copy_constructible_v<T>,
				"ECS保存スナップショットにはコピー可能なComponentが必要です");
			info.copyConstruct = [](void* dst, const void* src) {
				new (dst) T(*static_cast<const T*>(src));
				};
			info.moveConstruct = [](void* dst, void* src) { new (dst) T(std::move(*(T*)src)); };
		}
		info.releaseExternal = []([[maybe_unused]] ECSWorld& world,
			[[maybe_unused]] const Entity& entity, [[maybe_unused]] void* ptr) {

			if constexpr (ComponentTypeTraits::ResolveStorageKind<T>() != ComponentStorageKind::Buffer) {

				T& component = *static_cast<T*>(ptr);
				if constexpr (ComponentTypeTraits::ResolveECSHooks<T>()) {
					T::ReleaseStorage(world, entity, component);
				} else if constexpr (requires(
					ECSWorld& hookWorld, const Entity& hookEntity, T& hookComponent) {
						ReleaseComponentStorage(
							hookWorld, hookEntity, hookComponent);
					}) {
					ReleaseComponentStorage(world, entity, component);
				}
			}
			};
		if constexpr (!ComponentTypeTraits::ResolveSerializable<T>()) {

			// Runtime専用Componentは保存経路を生成せず、JSON変換要件を持たせない
			info.toJson = []([[maybe_unused]] const ECSWorld& world,
				[[maybe_unused]] const Entity& entity,
				[[maybe_unused]] const void* obj, nlohmann::json& out) {
				out = nlohmann::json::object();
				};
			info.fromJson = []([[maybe_unused]] ECSWorld& world,
				[[maybe_unused]] const Entity& entity,
				[[maybe_unused]] void* obj, [[maybe_unused]] const nlohmann::json& in) {
				};
		} else if constexpr (
			ComponentTypeTraits::ResolveStorageKind<T>() == ComponentStorageKind::Buffer) {

			info.toJson = []([[maybe_unused]] const ECSWorld& world,
				[[maybe_unused]] const Entity& entity,
				const void* obj, nlohmann::json& out) {

				DynamicBuffer<const T> buffer(static_cast<const DynamicBufferHeader*>(obj));
				out = nlohmann::json::array();
				for (const T& element : buffer.GetSpan()) {
					out.push_back(element);
				}
				};
			info.fromJson = []([[maybe_unused]] ECSWorld& world,
				[[maybe_unused]] const Entity& entity,
				void* obj, const nlohmann::json& in) {

				DynamicBuffer<T> buffer(static_cast<DynamicBufferHeader*>(obj));
				buffer.Clear();
				if (!in.is_array()) {
					return;
				}
				buffer.Reserve(static_cast<uint32_t>(in.size()));
				for (const nlohmann::json& element : in) {
					buffer.Add(element.get<T>());
				}
				};
		} else {

			info.toJson = []([[maybe_unused]] const ECSWorld& world,
				[[maybe_unused]] const Entity& entity,
				const void* obj, nlohmann::json& out) {

				const T& component = *static_cast<const T*>(obj);
				if constexpr (ComponentTypeTraits::ResolveECSHooks<T>()) {
					T::SerializeECS(world, entity, component, out);
				} else if constexpr (requires(
					const ECSWorld& hookWorld, const Entity& hookEntity,
					const T& hookComponent, nlohmann::json& hookOut) {
						SerializeComponent(
							hookWorld, hookEntity, hookComponent, hookOut);
					}) {
					SerializeComponent(world, entity, component, out);
				} else {
					out = component;
				}
				};
			info.fromJson = []([[maybe_unused]] ECSWorld& world,
				[[maybe_unused]] const Entity& entity,
				void* obj, const nlohmann::json& in) {

				T& component = *static_cast<T*>(obj);
				if constexpr (ComponentTypeTraits::ResolveECSHooks<T>()) {
					T::DeserializeECS(world, entity, in, component);
				} else if constexpr (requires(
					ECSWorld& hookWorld, const Entity& hookEntity,
					const nlohmann::json& hookIn, T& hookComponent) {
						DeserializeComponent(
							hookWorld, hookEntity, hookIn, hookComponent);
					}) {
					DeserializeComponent(world, entity, in, component);
				} else {
					component = in.get<T>();
				}
				};
		}

		// Manifestの固定順で追加
		infos_.emplace_back(info);
		nameToID_[info.name] = info.id;
		typeKeyToID_[GetTypeKey<T>()] = info.id;
	}

	template <typename T>
	inline uint32_t ComponentTypeRegistry::GetID() const {

		static const uint32_t cachedID = [&]() {
			auto it = typeKeyToID_.find(GetTypeKey<T>());
			Assert::Call(it != typeKeyToID_.end(), "ComponentManifestへ型を登録してください");
			return it->second;
			}();
		return cachedID;
	}

	template <typename T>
	inline const void* ComponentTypeRegistry::GetTypeKey() {

		static const uint8_t key = 0;
		return &key;
	}
} // Engine

