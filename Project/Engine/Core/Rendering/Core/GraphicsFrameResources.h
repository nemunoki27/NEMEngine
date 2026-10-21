#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstdint>

namespace Engine {

	class GraphicsFrameResourceRegistration;

	//============================================================================
	//	GraphicsFrameResourceRegistry class
	//	フレーム別資源の所有者へ未使用スロットの解放を通知する
	//============================================================================
	class GraphicsFrameResourceRegistry {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		GraphicsFrameResourceRegistry() = default;
		~GraphicsFrameResourceRegistry();
		GraphicsFrameResourceRegistry(const GraphicsFrameResourceRegistry&) = delete;
		GraphicsFrameResourceRegistry& operator=(const GraphicsFrameResourceRegistry&) = delete;

		// 変更に必要な候補を用意し、失敗時は候補だけを戻す
		bool Prepare(uint32_t count);
		void Cancel(uint32_t count);
		// GPU完了後に未使用スロットを解放する
		void ReleaseInactive(uint32_t count);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		friend class GraphicsFrameResourceRegistration;
		GraphicsFrameResourceRegistration* first_ = nullptr;
		uint64_t revision_ = 0;
	};

	//============================================================================
	//	GraphicsFrameResourceRegistration class
	//	所有者の寿命と移動に合わせて通知先を接続する
	//============================================================================
	class GraphicsFrameResourceRegistration {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		using ReleaseFunction = void (*)(void*, uint32_t);
		using PrepareFunction = bool (*)(void*, uint32_t);

		GraphicsFrameResourceRegistration() = default;
		~GraphicsFrameResourceRegistration();
		GraphicsFrameResourceRegistration(const GraphicsFrameResourceRegistration&) = delete;
		GraphicsFrameResourceRegistration& operator=(const GraphicsFrameResourceRegistration&) = delete;

		// 所有者と解放処理を登録する
		void Bind(GraphicsFrameResourceRegistry& registry, void* owner, ReleaseFunction release,
			PrepareFunction prepare = nullptr, ReleaseFunction cancel = nullptr);
		// 所有者を破棄する前に接続を解除する
		void Reset();
		// 移動した所有者へ接続を引き継ぐ
		void Swap(GraphicsFrameResourceRegistration& other, void* owner, void* otherOwner) noexcept;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		friend class GraphicsFrameResourceRegistry;
		GraphicsFrameResourceRegistry* registry_ = nullptr;
		GraphicsFrameResourceRegistration* previous_ = nullptr;
		GraphicsFrameResourceRegistration* next_ = nullptr;
		void* owner_ = nullptr;
		ReleaseFunction release_ = nullptr;
		PrepareFunction prepare_ = nullptr;
		ReleaseFunction cancel_ = nullptr;
		uint64_t visitedRevision_ = 0;
	};
}
