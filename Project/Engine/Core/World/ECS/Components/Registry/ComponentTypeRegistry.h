#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Core/ComponentType.h>
#include <Engine/Core/World/ECS/Config/ECSConfig.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>

// c++
#include <string>
#include <vector>
#include <unordered_map>

namespace Engine {

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
		info.size = sizeof(T);
		info.align = alignof(T);

		// 型Tの関数を登録
		info.constructDefault = [](void* ptr) { new (ptr) T(); };
		info.destroy = [](void* ptr) { ((T*)ptr)->~T(); };
		info.moveConstruct = [](void* dst, void* src) { new (dst) T(std::move(*(T*)src)); };
		info.to_json = [](const void* obj, nlohmann::json& out) { out = *(const T*)obj; };
		info.from_json = [](void* obj, const nlohmann::json& in) { *(T*)obj = in.get<T>(); };

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

