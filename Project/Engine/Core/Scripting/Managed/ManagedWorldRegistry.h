#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/ManagedScriptTypes.h>

// c++
#include <cstdint>
#include <vector>

namespace Engine {

	// front
	class ECSWorld;

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

		//--------- types --------------------------------------------------------

		// 登録枠、free listで再利用し解除時にgenerationを進める
		struct Slot {

			ECSWorld* world = nullptr;
			uint32_t generation = 1; // 1始まりでゼロ初期化Entityのworldハンドルと衝突させない
			bool inUse = false;
		};

		//--------- variables ----------------------------------------------------

		std::vector<Slot> slots_;
		std::vector<uint32_t> free_;
	};
} // Engine
