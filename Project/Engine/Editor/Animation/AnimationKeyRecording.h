#pragma once

//============================================================================
//	include
//============================================================================
#include "AnimationPropertyEditCapture.h"
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>

namespace Engine {

	// 変更したプロパティだけを指定時刻のキーへ反映する
	bool RecordAnimationProperties(AnimationClipAsset& clip, std::span<const RecordedAnimationProperty> changes,
		double time, double frameRate);
	// 値型に対応するキーを同時刻なら更新する
	void SetAnimationPropertyKey(AnimationCurveTrack& track, float time, const AnimationPropertyValue& value);
	// 相対適用と加算乗算を逆変換して素材のキー値を得る
	bool ResolveRecordedAnimationValue(const AnimationClipAsset& clip, const AnimationCurveTrack* track,
		const RecordedAnimationProperty& change, const AnimationPropertyValue& desired, double time, AnimationPropertyValue& value);
}
