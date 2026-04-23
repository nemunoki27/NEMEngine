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
		Vector3,
		Vector2
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

	// C#と共有するVector2
	struct ManagedVector2 {

		float x = 0.0f;
		float y = 0.0f;
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
		using GetVector2Callback = ManagedVector2(__cdecl*)();
		using GetVector3Callback = ManagedVector3(__cdecl*)(ManagedNativeEntity);
		using SetVector3Callback = void(__cdecl*)(ManagedNativeEntity, ManagedVector3);
		using GetQuaternionCallback = ManagedQuaternion(__cdecl*)(ManagedNativeEntity);
		using SetQuaternionCallback = void(__cdecl*)(ManagedNativeEntity, ManagedQuaternion);
		using LogCallback = void(__cdecl*)(int32_t, const char*);
		using GetInputButtonCallback = int32_t(__cdecl*)(int32_t);
		using GetNativeBoolCallback = int32_t(__cdecl*)();
		using IsAliveCallback = int32_t(__cdecl*)(ManagedNativeEntity);
		using GetBoolCallback = int32_t(__cdecl*)(ManagedNativeEntity);
		using SetBoolCallback = void(__cdecl*)(ManagedNativeEntity, int32_t);
		using CopyStringCallback = int32_t(__cdecl*)(ManagedNativeEntity, char*, int32_t);
		using SetStringCallback = void(__cdecl*)(ManagedNativeEntity, const char*);
		using GetEntityCallback = ManagedNativeEntity(__cdecl*)(ManagedNativeEntity);
		using SetParentCallback = void(__cdecl*)(ManagedNativeEntity, ManagedNativeEntity);

		GetDeltaTimeCallback getDeltaTime = nullptr;
		GetDeltaTimeCallback getFixedDeltaTime = nullptr;
		LogCallback log = nullptr;
		GetInputButtonCallback getKey = nullptr;
		GetInputButtonCallback getKeyDown = nullptr;
		GetInputButtonCallback getKeyUp = nullptr;
		GetInputButtonCallback getMouseButton = nullptr;
		GetInputButtonCallback getMouseButtonDown = nullptr;
		GetInputButtonCallback getMouseButtonUp = nullptr;
		GetVector2Callback getMousePosition = nullptr;
		GetVector2Callback getMouseDelta = nullptr;
		GetDeltaTimeCallback getMouseWheel = nullptr;
		GetInputButtonCallback getGamepadButton = nullptr;
		GetInputButtonCallback getGamepadButtonDown = nullptr;
		GetNativeBoolCallback isGamepadConnected = nullptr;
		GetVector2Callback getLeftStick = nullptr;
		GetVector2Callback getRightStick = nullptr;
		GetDeltaTimeCallback getLeftTrigger = nullptr;
		GetDeltaTimeCallback getRightTrigger = nullptr;
		IsAliveCallback isAlive = nullptr;
		CopyStringCallback copyName = nullptr;
		SetStringCallback setName = nullptr;
		GetBoolCallback getActiveSelf = nullptr;
		SetBoolCallback setActiveSelf = nullptr;
		GetBoolCallback getActiveInHierarchy = nullptr;
		GetEntityCallback getParent = nullptr;
		GetEntityCallback getFirstChild = nullptr;
		GetEntityCallback getNextSibling = nullptr;
		SetParentCallback setParent = nullptr;
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
