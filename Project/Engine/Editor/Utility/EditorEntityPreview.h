#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>

// c++
#include <memory>

namespace Engine {

	class ECSWorld;
	class ECSWorldLifetime;

	//============================================================================
	//	EditorEntityPreview class
	//	仮Entityの所有と開始Worldの寿命を管理する
	//============================================================================
	class EditorEntityPreview {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		EditorEntityPreview() = default;
		~EditorEntityPreview();
		EditorEntityPreview(const EditorEntityPreview&) = delete;
		EditorEntityPreview& operator=(const EditorEntityPreview&) = delete;

		// 仮Entityとその子孫の所有を受け取る
		bool Begin(ECSWorld& world, Entity entity);
		// 仮Entityを破棄してプレビューを終了する
		void End();
		// 確定したEntityの所有を呼出元へ渡す
		Entity Release();

		//--------- accessor -----------------------------------------------------

		// 開始したWorldと同じか
		bool BelongsTo(const ECSWorld& world) const;
		// 生存する仮Entityを取得する
		Entity GetEntity() const;
		// 仮Entityが生存しているか
		bool IsActive() const;

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 仮Entityを作成したWorld
		ECSWorld* world_ = nullptr;
		// World破棄後の参照を防ぐ寿命
		std::weak_ptr<const ECSWorldLifetime> lifetime_;
		// 所有する仮Entity
		Entity entity_ = Entity::Null();

		//--------- functions ----------------------------------------------------

		// Entityを破棄せず所有情報を解除する
		void Reset();
	};
} // Engine
