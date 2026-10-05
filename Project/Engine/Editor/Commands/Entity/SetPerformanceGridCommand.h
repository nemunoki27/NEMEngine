#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Core/IEditorCommand.h>
#include <Engine/Editor/Commands/Entity/EditorEntitySnapshot.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>

// c++
#include <vector>

namespace Engine {

	//============================================================================
	//	SetPerformanceGridCommand class
	//	描画負荷確認用グリッドの生成と取消
	//============================================================================
	class SetPerformanceGridCommand final : public IEditorCommand {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SetPerformanceGridCommand(UUID rootStableUUID, AssetID model, int32_t gridCountXZ, int32_t gridCountY, float gridWidth,
			bool playSkinnedAnimation, bool placePointLights, bool pointLightShadows, int32_t pointLightCount,
			float pointLightIntensity, float pointLightRadius, float pointLightDecay,
			std::vector<MeshSubMeshLayoutItem> layout);
		explicit SetPerformanceGridCommand(UUID rootStableUUID);
		~SetPerformanceGridCommand() override = default;

		// グリッドを生成する
		bool Execute(EditorCommandContext& context) override;
		// 生成前のグリッドへ戻す
		void Undo(EditorCommandContext& context) override;
		// 同じUUIDでグリッドを再生成する
		bool Redo(EditorCommandContext& context) override;
		// 同じグリッドへの連続再配置を1件のUndoへまとめる
		bool CanCoalesce(const IEditorCommand& next) const override;
		// 最初の取消状態を保って再配置する
		bool ExecuteCoalesced(IEditorCommand& next, EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "SetPerformanceGrid"; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 生成ルートの固定UUID
		UUID rootStableUUID_{};
		// 初回に確定したSceneの所属
		UUID sceneInstanceID_{};
		// 初回に確定したScene Asset
		AssetID sceneAsset_{};
		// Scene所属の確定済み状態
		bool sceneOwnerCaptured_ = false;
		// 配置するMesh
		AssetID model_{};
		// XZ方向の配置数
		int32_t gridCountXZ_ = 1;
		// Y方向の配置数
		int32_t gridCountY_ = 1;
		// 配置間隔
		float gridWidth_ = 1.0f;

		// 骨アニメーションの有効状態
		bool playSkinnedAnimation_ = false;
		// ライトの配置要求
		bool placePointLights_ = false;
		// ライトの影の有効状態
		bool pointLightShadows_ = false;
		// ライトの配置数
		int32_t pointLightCount_ = 200;
		// ライトの強度
		float pointLightIntensity_ = 1.0f;
		// ライトの半径
		float pointLightRadius_ = 8.0f;
		// ライトの減衰
		float pointLightDecay_ = 1.0f;
		// 生成グリッドの削除要求
		bool deleteGrid_ = false;

		// 解析済みSubMeshの構成
		std::vector<MeshSubMeshLayoutItem> layout_;
		// Redoで再利用するモデルのUUID
		std::vector<UUID> modelStableUUIDs_;
		// Redoで再利用するライトのUUID
		std::vector<UUID> pointLightStableUUIDs_;
		// 取消時に復元する生成前の状態
		std::vector<EditorEntityTreeSnapshot> previousSnapshots_;
		// 初回の状態の取得済み判定
		bool previousCaptured_ = false;

		//--------- functions ----------------------------------------------------

		// グリッドを現在の編集ワールドへ生成する
		bool CreateGrid(EditorCommandContext& context);
		// 生成と再配置に共通する入力を検証する
		bool IsValidRequest() const;
		// 同数の生成済みエンティティを破棄せず設定だけ更新する
		bool TryUpdateGrid(EditorCommandContext& context, const Entity& root);
		// 初回のScene所属を確定する
		void CaptureSceneOwner(const EditorCommandContext& context, ECSWorld& world);
		// Entityへ初回のScene所属を設定する
		void SetSceneOwner(ECSWorld& world, const Entity& entity) const;
	};
} // Engine
