#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Type/ComponentType.h>
#include <Engine/Core/ECS/Config/ECSConfig.h>
#include <Engine/Core/UUID/TypeHash.h>
#include <Engine/Logger/Assert.h>

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
		//========================================================================
		//	public Methods
		//========================================================================

		ComponentTypeRegistry() = default;
		~ComponentTypeRegistry() = default;

		// コンポーネントの種類を登録
		template <typename T>
		uint32_t Register(const std::string_view& name);

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
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::vector<ComponentTypeInfo> infos_;
		std::unordered_map<std::string, uint32_t> nameToID_;
		std::unordered_map<uint32_t, uint32_t> typeKeyToID_;
	};

	//============================================================================
	//	ComponentTypeRegistry macros
	//============================================================================
#define ENGINE_REGISTER_COMPONENT(T, NameLiteral) \
    inline const uint32_t kCompID_##T = Engine::ComponentTypeRegistry::GetInstance().Register<T>(NameLiteral);

	//============================================================================
	//	ComponentTypeRegistry templateMethods
	//============================================================================

	template <typename T>
	inline uint32_t ComponentTypeRegistry::Register(const std::string_view& name) {

		// 既に登録済みならそのIDを返す
		auto it = nameToID_.find(std::string(name));
		if (it != nameToID_.end()) {
			return it->second;
		}

		// 登録されている種類の数が上限に達していないか
		Assert::Call(GetComponentTypeCount() < kMaxComponentTypes, "kMaxComponentTypesを増やしてください");

		// コンポーネントの情報を作成
		ComponentTypeInfo info{};
		info.name = std::string(name);
		info.id = GetComponentTypeCount();
		info.size = sizeof(T);
		info.align = alignof(T);

		// 型Tの関数を登録
		info.constructDefault = [](void* ptr) { new (ptr) T(); };
		info.destroy = [](void* ptr) { ((T*)ptr)->~T(); };
		info.moveConstruct = [](void* dst, void* src) { new (dst) T(std::move(*(T*)src)); };
		info.to_json = [](const void* obj, nlohmann::json& out) { out = *(const T*)obj; };
		info.from_json = [](void* obj, const nlohmann::json& in) { *(T*)obj = in.get<T>(); };

		// 追加してIDを返す
		infos_.emplace_back(info);
		nameToID_[info.name] = info.id;
		// ハッシュ値からIDへのマッピングも登録
		typeKeyToID_[EntityToTypeHash(typeid(T).name())] = info.id;
		return info.id;
	}

	template <typename T>
	inline uint32_t ComponentTypeRegistry::GetID() const {

		auto it = typeKeyToID_.find(EntityToTypeHash(typeid(T).name()));
		Assert::Call(it != typeKeyToID_.end(), "ComponentTypeRegistry::Register<T>() を先に呼んでください");
		return it->second;
	}
} // Engine