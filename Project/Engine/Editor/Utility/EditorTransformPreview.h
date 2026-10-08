#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Editor/Commands/Core/IEditorCommand.h>

// c++
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace Engine {

	class ECSWorld;
	class ECSWorldLifetime;

	//============================================================================
	//	EditorTransformPreview class
	//	開始Worldと姿勢を保持して未確定編集を戻す
	//============================================================================
	class EditorTransformPreview {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		EditorTransformPreview() = default;
		~EditorTransformPreview();
		EditorTransformPreview(const EditorTransformPreview&) = delete;
		EditorTransformPreview& operator=(const EditorTransformPreview&) = delete;

		// 編集対象と開始値を固定する
		bool Begin(ECSWorld& world, std::span<const Entity> targets, bool runtimeOnly);
		// 編集用Worldの開始値を戻して終了する
		void Cancel();
		// 現在値を維持して所有情報を解除する
		void Release();
		// 変更した姿勢を一件の履歴へまとめる
		std::unique_ptr<IEditorCommand> BuildCommand(const ECSWorld& world) const;

		//--------- accessor -----------------------------------------------------

		// 編集を開始したWorldと同じか
		bool BelongsTo(const ECSWorld& world) const;
		// 開始時の対象と姿勢を取得する
		const std::vector<std::pair<UUID, TransformComponent>>& GetSnapshots() const { return snapshots_; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 開始Worldの借用
		ECSWorld* world_ = nullptr;
		// World破棄後の参照を防ぐ寿命
		std::weak_ptr<const ECSWorldLifetime> lifetime_;
		// 操作開始時の対象と姿勢
		std::vector<std::pair<UUID, TransformComponent>> snapshots_;
		// 実行用Worldでは取消時も値を維持する
		bool runtimeOnly_ = false;
	};
}
