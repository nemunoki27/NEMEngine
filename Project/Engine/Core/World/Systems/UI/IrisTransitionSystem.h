#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>
#include <Engine/Core/World/Components/UI/IrisTransitionComponent.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

namespace Engine {

	//============================================================================
	//	IrisTransitionSystem class
	//	アイリス遷移の再生とScreenUI描画Entityを管理する
	//============================================================================
	class IrisTransitionSystem :
		public ISystem {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		IrisTransitionSystem() = default;
		~IrisTransitionSystem() = default;

		void OnWorldEnter(ECSWorld& world, SystemContext& context) override;
		void OnWorldExit(ECSWorld& world, SystemContext& context) override;
		void Update(ECSWorld& world, SystemContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "IrisTransitionSystem"; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		struct RuntimeSettings {

			Vector2 screenPosition{};
			Color4 transitionColor = Color4(0.0f, 0.0f, 0.0f, 1.0f);
			float edgeSoftness = 8.0f;
			bool invertMask = false;
			float irisOutDuration = 0.5f;
			EasingType irisOutEasing = EasingType::EaseInOutSine;
			float irisInDuration = 0.5f;
			EasingType irisInEasing = EasingType::EaseInOutSine;
			bool blockInput = true;
			bool autoIrisInAfterSceneTransition = false;
			bool useUnscaledTime = true;
		};

		//--------- variables ----------------------------------------------------

		Entity renderEntity_ = Entity::Null();
		UUID ownerUUID_{};
		UUID activeSceneInstanceID_{};
		RuntimeSettings settings_{};

		IrisTransitionState state_ = IrisTransitionState::Open;
		float progress_ = 0.0f;
		float startProgress_ = 0.0f;
		float targetProgress_ = 0.0f;
		float elapsed_ = 0.0f;
		float duration_ = 0.0f;
		EasingType easing_ = EasingType::Linear;
		bool inputBlocked_ = false;
		bool pendingSceneTransition_ = false;

		bool editCommandPreview_ = false;
		float editAuthoringProgress_ = 0.0f;

		//--------- functions ----------------------------------------------------

		// 描画用Entityを作成する
		void EnsureRenderEntity(ECSWorld& world);
		// 描画用Entityを破棄する
		void DestroyRenderEntity(ECSWorld& world);
		// 最新要求を取り出して再生状態へ反映する
		bool ConsumeLatestCommand(ECSWorld& world, WorldMode mode);
		// 単一シーン切り替え後の状態を更新する
		bool UpdateSceneTransition(ECSWorld& world);
		// 編集中プレビュー設定を反映する
		bool UpdateEditPreview(ECSWorld& world);
		// 再生元コンポーネントの設定を反映する
		void RefreshOwnerSettings(ECSWorld& world);
		// 補間再生を進める
		void UpdatePlayback(float deltaTime);
		// 描画パラメータを更新する
		void UpdateRenderEntity(ECSWorld& world, bool visible);
		// 全コンポーネントへランタイム状態を同期する
		void SyncComponentRuntime(ECSWorld& world);
		// コンポーネント設定をコピーする
		void CopySettings(const IrisTransitionComponent& component);
		// 指定進行度への補間を開始する
		void StartPlayback(IrisTransitionState state, float target,
			float duration, EasingType easing);
		// 再生を開いた状態へ戻す
		void ResetPlayback();
		// 再生中か
		bool IsPlaying() const;
		// シーン遷移を占有する状態か
		bool IsTransitionActive() const;
	};
} // Engine
