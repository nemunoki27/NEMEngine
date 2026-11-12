#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>

// c++
#include <memory>
#include <span>
#include <optional>

namespace Engine {

	struct RecordedAnimationProperty {

		AnimationPropertyBinding binding;
		AnimationPropertyValue before{}, after{};
		std::optional<AnimationPropertyValue> baseline;
		Quaternion originRotation = Quaternion::Identity();
		float originAngle = 0.0f;
	};

	//============================================================================
	//	IAnimationPropertyRecorder class
	//	ユーザーが確定した変更だけをアニメーションへ渡す
	//============================================================================
	class IAnimationPropertyRecorder {
	public:
		virtual ~IAnimationPropertyRecorder() = default;
		virtual bool Record(std::span<const RecordedAnimationProperty> changes) = 0;
		virtual bool RestoreHistory(bool redo) = 0;
	};

	//============================================================================
	//	AnimationPropertyEditCapture namespace
	//	InspectorとGizmoの編集区間を共通の記録先へ接続する
	//============================================================================
	namespace AnimationPropertyEditCapture {

		// 記録先と対象をWorldの寿命内で登録する
		void SetRecorder(ECSWorld& world, Entity target, std::weak_ptr<IAnimationPropertyRecorder> recorder,
			const AnimationPropertyQueryContext& query);
		// 同じ記録先が所有する編集区間を終了する
		void ClearRecorder(ECSWorld& world, const IAnimationPropertyRecorder* recorder);
		// ユーザー編集を始める直前の値を保持する
		void Begin(ECSWorld& world, Entity target, std::string_view component);
		// 変更した値をキーへ渡して記録対象のScene値を戻す
		bool Commit(ECSWorld& world, Entity target, std::string_view component);
		// 確定しなかった編集を破棄する
		void Cancel(ECSWorld& world, Entity target, std::string_view component);
		// ユーザーが編集している間だけ再評価を保留する
		bool IsEditing(const ECSWorld& world);
		bool HasRecorder(const ECSWorld& world);
		bool RestoreHistory(ECSWorld& world, bool redo);
	}
}
