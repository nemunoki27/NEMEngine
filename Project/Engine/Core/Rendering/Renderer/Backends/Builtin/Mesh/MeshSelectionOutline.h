#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/World/Components/Rendering/InvertedHullOutlineComponent.h>

// c++
#include <cstdint>

namespace Engine {

	// front
	class ECSWorld;

	//============================================================================
	//	MeshSelectionOutline class
	//	選択中メッシュのアウトラインプレビュー要求。
	//	LineRendererと同じく、エディタ描画でフレームごとに要求を積み、描画パスが消費する。
	//============================================================================
	class MeshSelectionOutline {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		static MeshSelectionOutline& GetInstance() {

			static MeshSelectionOutline instance;
			return instance;
		}

		// フレーム開始時に要求をクリアする
		void BeginFrame() { active_ = false; }

		// プレビュー要求を登録する。submeshIndexが負ならエンティティ全体
		void Request(ECSWorld* world, const Entity& entity, int32_t submeshIndex,
			const InvertedHullOutlineComponent& params) {

			world_ = world;
			entity_ = entity;
			submeshIndex_ = submeshIndex;
			params_ = params;
			active_ = true;
		}

		//--------- accessor -----------------------------------------------------

		bool IsActive() const { return active_ && world_ != nullptr; }
		ECSWorld* GetWorld() const { return world_; }
		const Entity& GetEntity() const { return entity_; }
		// 負の場合はエンティティ全体、0以上なら対象サブメッシュ限定
		int32_t GetSubMeshIndex() const { return submeshIndex_; }
		const InvertedHullOutlineComponent& GetParams() const { return params_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ECSWorld* world_ = nullptr;
		Entity entity_ = Entity::Null();
		int32_t submeshIndex_ = -1;
		InvertedHullOutlineComponent params_{};
		bool active_ = false;
	};
} // Engine
