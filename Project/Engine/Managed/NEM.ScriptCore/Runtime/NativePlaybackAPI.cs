namespace NEMEngine;

using static NEMEngine.NativeAPI;

// 接続済みcallbackを用途別に呼び出す
internal static unsafe class NativePlaybackAPI {

    // 指定Clipの再生時間を返す
    internal static float ReadSkinnedAnimationDuration(NativeEntity entity, string clipName) {

        if (GetSkinnedAnimationDuration == null) {
            return 0.0f;
        }
        string safe = clipName ?? string.Empty;
        byte[] bytes = ManagedUTF8Transfer.GetNullTerminatedBytes(safe);
        fixed (byte* ptr = bytes) {
            return GetSkinnedAnimationDuration(entity, ptr);
        }
    }

    // 指定Clipを先頭から再生する
    internal static void PlaySkinnedAnimationClip(NativeEntity entity, string clipName) {

        if (PlaySkinnedAnimation == null) {
            return;
        }
        string safe = clipName ?? string.Empty;
        byte[] bytes = ManagedUTF8Transfer.GetNullTerminatedBytes(safe);
        fixed (byte* ptr = bytes) {
            PlaySkinnedAnimation(entity, ptr);
        }
    }

    // 実行中のClip名を読む
    internal static string ReadSkinnedAnimationCurrentClip(NativeEntity entity) {

        if (CopySkinnedAnimationCurrentClip == null) {
            return string.Empty;
        }
        return ManagedUTF8Transfer.ReadString(CopySkinnedAnimationCurrentClip, entity);
    }

    // Animationの再生状態を取得する
    internal static NativeSkinnedAnimationRuntimeState ReadSkinnedAnimationRuntimeState(NativeEntity entity) {

        NativeSkinnedAnimationRuntimeState state = default;
        if (GetSkinnedAnimationRuntimeState != null) {
            GetSkinnedAnimationRuntimeState(entity, &state);
        }
        return state;
    }

    // Audioの再生を開始する
    internal static void AudioPlayCall(NativeEntity entity) {

        if (AudioPlay != null) {
            AudioPlay(entity);
        }
    }

    // 指定Clipを重ねて再生する
    internal static void AudioPlayOneShotCall(NativeEntity entity, AssetGUID clipID, float volumeScale) {

        if (AudioPlayOneShot != null) {
            AudioPlayOneShot(entity, clipID, volumeScale);
        }
    }

    // Audioの再生を一時停止する
    internal static void AudioPauseCall(NativeEntity entity) {

        if (AudioPause != null) {
            AudioPause(entity);
        }
    }

    // Audioの再生を再開する
    internal static void AudioUnPauseCall(NativeEntity entity) {

        if (AudioUnPause != null) {
            AudioUnPause(entity);
        }
    }

    // Audioの再生を停止する
    internal static void AudioStopCall(NativeEntity entity) {

        if (AudioStop != null) {
            AudioStop(entity);
        }
    }

    // Audioの再生状態を返す
    internal static bool AudioIsPlayingCall(NativeEntity entity) =>
        AudioIsPlaying != null && AudioIsPlaying(entity) != 0;
}
