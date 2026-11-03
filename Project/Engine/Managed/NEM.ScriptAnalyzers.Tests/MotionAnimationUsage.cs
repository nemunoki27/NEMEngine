using NEMEngine;

namespace NEM.ScriptAnalyzers.Tests;

// ゲーム側で敵選択・当たり判定・斬撃生成を分担する使用例。
internal static class MotionAnimationUsage {
    internal static void BeginApproach(MotionAnimation motion, GameObject enemy) {
        // 敵の手前まで接近し、開始時の高さを維持する。
        var settings = motion.GetSettings();
        settings.Mode = MotionAnimationMode.ApproachTarget;
        settings.Plane = MotionAnimationPlane.GroundXZ;
        settings.Duration = 0.25f;
        settings.StoppingDistance = 1.2f;
        settings.Easing = EasingType.EaseOutQuad;
        settings.TrackTarget = false;
        settings.ApplyRotation = true;
        settings.Rotation = MotionAnimationRotation.LookAtTarget;
        settings.Repeat = SimpleAnimationRepeat.Once;
        motion.SetSettings(settings);
        motion.SetTarget(enemy);
        motion.Play();
    }

    internal static void BeginCombo(MotionAnimation motion) {
        // 3区間の終点を、攻撃開始時の位置と向きを基準に指定する。
        var settings = motion.GetSettings();
        settings.Mode = MotionAnimationMode.Segments;
        settings.Plane = MotionAnimationPlane.GroundXZ;
        settings.ApplyRotation = false;
        settings.Segments = [
            new() { Offset = new(-0.5f, 0, 1), Duration = 0.15f, Easing = EasingType.EaseOutQuad },
            new() { Offset = new(0.5f, 0, 2), Duration = 0.15f, Easing = EasingType.EaseInOutSine },
            new() { Offset = new(0, 0, 3), Duration = 0.2f, Easing = EasingType.EaseOutQuad }
        ];
        settings.Repeat = SimpleAnimationRepeat.Once;
        motion.SetSettings(settings);
        motion.Play();
    }

    internal static void PollAttackEvents(MotionAnimation motion, Action emitSecondSlash, Action finishAttack) {
        // LateUpdateなどで通知を一度だけ消費し、区間0の終了に斬撃を合わせる。
        var events = motion.ConsumeEvents();
        if (events.Segments.Length > 0)
            for (ulong i = 0; i < events.Segments[0]; ++i) emitSecondSlash();
        if (events.Completions > 0) finishAttack();
    }

    internal static Vector3 ReadMovementForCollision(MotionAnimation motion) {
        // AutoApply=falseなら、得た移動差分をゲーム側の移動・衝突処理へ渡せる。
        var state = motion.GetState();
        return state.DeltaPosition;
    }
}
