#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/ManagedScriptTypes.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>

// c++
#include <cstdint>
#include <unordered_map>

namespace Engine {

	// front
	class ECSWorld;
	class ECSWorldLifetime;

	//============================================================================
	//	ManagedWorldRegistry class
	//	C#へ生のECSWorld*を渡さないための世代付きworldハンドル管理
	//============================================================================
	class ManagedWorldRegistry {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		// worldを登録してハンドルを返す、登録済みなら既存ハンドルを返す
		ManagedWorldHandle Register(ECSWorld& world);
		// 登録を解除する、以降そのハンドルはTryResolveでnullptrになる
		void Unregister(ManagedWorldHandle handle);

		//--------- accessor -----------------------------------------------------

		// ハンドルからworldを解決する、古いまたは未登録ハンドルはnullptr
		ECSWorld* TryResolve(ManagedWorldHandle handle) const;
		// world実体から現在のハンドルを引く、未登録ならnullハンドル
		ManagedWorldHandle TryGetHandle(const ECSWorld& world) const;
		// ハンドルが現在も有効か
		bool IsAlive(ManagedWorldHandle handle) const;

		static ManagedWorldRegistry& GetInstance();
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		ManagedWorldRegistry() = default;
		~ManagedWorldRegistry() = default;

		//--------- structure ----------------------------------------------------

		struct WorldSlotTag;
		struct WorldSlot {

			ECSWorld* world = nullptr;
			std::shared_ptr<const ECSWorldLifetime> lifetime;
		};

		//--------- variables ----------------------------------------------------

		// Worldの参照を世代付きで登録する
		GenerationalPool<WorldSlot, WorldSlotTag> worlds_;
		// 登録済みWorldから公開ハンドルを引く
		std::unordered_map<const ECSWorld*, ManagedWorldHandle> handles_;

	};
} // Engine
