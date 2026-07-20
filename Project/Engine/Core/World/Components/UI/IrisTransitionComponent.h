#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Runtime/Context/EngineContext.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>
#include <Engine/Core/Foundation/Math/Color.h>
#include <Engine/Core/Foundation/Math/Vector2.h>

namespace Engine {

	//============================================================================
	//	IrisTransitionComponent structures
	//	スクリーン全体のアイリスインとアイリスアウトを制御する
	//============================================================================

	// アイリス遷移の再生状態
	enum class IrisTransitionState :
		uint8_t {

		Open,    // 開いた状態
		IrisOut, // 閉じる遷移を再生中
		Covered, // 画面全体を遮蔽した状態
		IrisIn,  // 開く遷移を再生中
		Stopped, // 現在の進行度で停止中
	};

	// アイリス遷移の再生要求
	enum class IrisTransitionCommand :
		uint8_t {

		None,        // 要求なし
		IrisOut,     // アイリスアウトを要求
		IrisIn,      // アイリスインを要求
		SetProgress, // 進行度の設定を要求
		Cancel,      // 現在位置での停止を要求
		Reset,       // 開いた状態への復帰を要求
	};

	struct IrisTransitionComponent {

		// 遷移処理を有効にするか
		bool enabled = true;

		// 遷移中心のスクリーン座標
		Vector2 screenPosition = EngineContext::GetWindowSetting().gameSizeFloat * 0.5f;
		// 画面の遮蔽色
		Color4 transitionColor = Color4(0.0f, 0.0f, 0.0f, 1.0f);
		// 境界のぼかし幅
		float edgeSoftness = 8.0f;
		// マスク位置を反転するか
		bool invertMask = false;

		// アイリスアウトの再生時間
		float irisOutDuration = 0.5f;
		// アイリスアウトのイージング
		EasingType irisOutEasing = EasingType::EaseInOutSine;
		// アイリスインの再生時間
		float irisInDuration = 0.5f;
		// アイリスインのイージング
		EasingType irisInEasing = EasingType::EaseInOutSine;

		// 再生中に入力を停止するか
		bool blockInput = true;
		// シーン切り替え後に自動でアイリスインするか
		bool autoIrisInAfterSceneTransition = false;
		// 非スケール時間で更新するか
		bool useUnscaledTime = true;

		// 編集中にプレビューするか
		bool previewInEditMode = false;
		// 編集中のプレビュー進行度
		float previewProgress = 0.0f;

		// ランタイム状態
		IrisTransitionState runtimeState = IrisTransitionState::Open;
		float runtimeProgress = 0.0f;
		IrisTransitionCommand runtimeCommand = IrisTransitionCommand::None;
		float runtimeCommandValue = 0.0f;
		uint64_t runtimeCommandSerial = 0;
		uint64_t runtimeEditPreviewSerial = 0;

		// アイリスアウトを要求する
		void IrisOut();
		// アイリスインを要求する
		void IrisIn();
		// 進行度を直接設定する
		void SetProgress(float progress);
		// 現在位置で再生を停止する
		void Cancel();
		// 開いた状態へ戻す
		void Reset();
		// 編集中プレビューの更新を要求する
		void RequestEditPreview();
	};

	// シーン設定のみを反映
	void ApplyIrisTransitionAuthoring(const IrisTransitionComponent& source,
		IrisTransitionComponent& destination);
	// ランタイム状態を初期化
	void ResetIrisTransitionRuntime(IrisTransitionComponent& component);

	void from_json(const nlohmann::json& in, IrisTransitionComponent& component);
	void to_json(nlohmann::json& out, const IrisTransitionComponent& component);

	ENGINE_REGISTER_COMPONENT(IrisTransitionComponent, "IrisTransition");
} // Engine
