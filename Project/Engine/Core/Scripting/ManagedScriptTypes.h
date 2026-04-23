#pragma once

//============================================================================
//	include
//============================================================================

// c++
#include <cstdint>
#include <string>

namespace Engine {

	//============================================================================
	//	ManagedScript structures
	//============================================================================

	// C#側のシリアライズフィールドの種類
	enum class ManagedSerializedFieldKind : int32_t {

		None = 0,
		Bool,
		Int,
		Float,
		Double,
		String,
		Vector3
	};

	// C#側から取得したシリアライズフィールド情報
	struct ManagedScriptField {

		std::string name;
		std::string displayName;
		ManagedSerializedFieldKind kind = ManagedSerializedFieldKind::None;
		bool isPublic = false;
		std::string defaultValueJson;
	};

	// C#へ渡すエンティティ参照
	struct ManagedNativeEntity {

		std::uintptr_t world = 0;
		uint32_t index = 0xFFFFFFFF;
		uint32_t generation = 0;
	};

	// C#と共有するVector3
	struct ManagedVector3 {

		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	};

	// C#と共有するQuaternion
	struct ManagedQuaternion {

		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float w = 1.0f;
	};

	// C#へ渡すネイティブAPI
	struct ManagedNativeApiTable {

		using GetDeltaTimeCallback = float(__cdecl*)();
		using GetVector3Callback = ManagedVector3(__cdecl*)(ManagedNativeEntity);
		using SetVector3Callback = void(__cdecl*)(ManagedNativeEntity, ManagedVector3);
		using GetQuaternionCallback = ManagedQuaternion(__cdecl*)(ManagedNativeEntity);
		using SetQuaternionCallback = void(__cdecl*)(ManagedNativeEntity, ManagedQuaternion);

		GetDeltaTimeCallback getDeltaTime = nullptr;
		GetVector3Callback getPosition = nullptr;
		SetVector3Callback setPosition = nullptr;
		GetVector3Callback getLocalPosition = nullptr;
		SetVector3Callback setLocalPosition = nullptr;
		GetVector3Callback getLocalScale = nullptr;
		SetVector3Callback setLocalScale = nullptr;
		GetQuaternionCallback getLocalRotation = nullptr;
		SetQuaternionCallback setLocalRotation = nullptr;
	};

	// C#側から受け取る固定長フィールド情報
	struct ManagedNativeSerializedFieldInfo {

		int32_t kind = 0;
		int32_t isPublic = 0;
		char name[128]{};
		char displayName[128]{};
		char defaultValueJson[512]{};
	};
} // Engine
