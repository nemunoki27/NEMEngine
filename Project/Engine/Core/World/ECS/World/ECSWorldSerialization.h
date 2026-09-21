#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>

// c++
#include <memory>
#include <string_view>
// json
#include <json.hpp>

namespace Engine {

	class ECSWorld;

	//============================================================================
	//	ECSWorldSerialization class
	//	ワールドの保存データ変換と保存用複製を行う
	//============================================================================
	class ECSWorldSerialization {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 保存データからコンポーネントを追加する
		static void AddComponentFromJson(ECSWorld& world,
			const Entity& entity, const std::string_view& typeName, const nlohmann::json& data);

		// 保存データを既存コンポーネントへ適用する
		static bool ApplyComponentJson(ECSWorld& world,
			const Entity& entity, const std::string_view& typeName, const nlohmann::json& data);

		// 保存用のワールドを複製する
		static std::unique_ptr<ECSWorld> CloneForSerialization(const ECSWorld& world);

		// エンティティの保存対象を出力する
		static void SerializeEntityComponents(const ECSWorld& world,
			const Entity& entity, nlohmann::json& outComponents);

		// 指定コンポーネントを保存データへ変換する
		static bool SerializeComponentToJson(const ECSWorld& world,
			const Entity& entity, const std::string_view& typeName, nlohmann::json& outData);
	};
} // Engine
