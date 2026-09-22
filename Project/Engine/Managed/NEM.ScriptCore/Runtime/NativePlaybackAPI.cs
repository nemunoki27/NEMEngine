using System.Text;

namespace NEMEngine;

using static NEMEngine.NativeAPI;

// 接続済みcallbackを用途別に呼び出す
internal static unsafe class NativePlaybackAPI {

    internal static float ReadSkinnedAnimationDuration(NativeEntity entity, string clipName) {
        if (GetSkinnedAnimationDuration == null) {
            return 0.0f;
        }
        string safe = clipName ?? string.Empty;
        byte[] bytes = new byte[Encoding.UTF8.GetByteCount(safe) + 1];
        Encoding.UTF8.GetBytes(safe, 0, safe.Length, bytes, 0);
        fixed (byte* ptr = bytes) {
            return GetSkinnedAnimationDuration(entity, ptr);
        }
    }

    internal static void PlaySkinnedAnimationClip(NativeEntity entity, string clipName) {
        if (PlaySkinnedAnimation == null) {
            return;
        }
        string safe = clipName ?? string.Empty;
        byte[] bytes = new byte[Encoding.UTF8.GetByteCount(safe) + 1];
        Encoding.UTF8.GetBytes(safe, 0, safe.Length, bytes, 0);
        fixed (byte* ptr = bytes) {
            PlaySkinnedAnimation(entity, ptr);
        }
    }

    internal static string ReadSkinnedAnimationCurrentClip(NativeEntity entity) {
        if (CopySkinnedAnimationCurrentClip == null) {
            return string.Empty;
        }

        int length = CopySkinnedAnimationCurrentClip(entity, null, 0);
        if (length <= 0) {
            return string.Empty;
        }

        byte[] bytes = new byte[length + 1];
        fixed (byte* buffer = bytes) {
            int written = CopySkinnedAnimationCurrentClip(
                entity, buffer, bytes.Length);
            return written <= 0 ? string.Empty :
                Encoding.UTF8.GetString(buffer, written);
        }
    }

    internal static NativeSkinnedAnimationRuntimeState ReadSkinnedAnimationRuntimeState(
        NativeEntity entity) {

        NativeSkinnedAnimationRuntimeState state = default;
        if (GetSkinnedAnimationRuntimeState != null) {
            GetSkinnedAnimationRuntimeState(entity, &state);
        }
        return state;
    }

    internal static void AudioPlayCall(NativeEntity entity) { if (AudioPlay != null) { AudioPlay(entity); } }

    internal static void AudioPlayOneShotCall(NativeEntity entity, AssetGUID clipID, float volumeScale) {
        if (AudioPlayOneShot != null) { AudioPlayOneShot(entity, clipID, volumeScale); }
    }

    internal static void AudioPauseCall(NativeEntity entity) { if (AudioPause != null) { AudioPause(entity); } }

    internal static void AudioUnPauseCall(NativeEntity entity) { if (AudioUnPause != null) { AudioUnPause(entity); } }

    internal static void AudioStopCall(NativeEntity entity) { if (AudioStop != null) { AudioStop(entity); } }

    internal static bool AudioIsPlayingCall(NativeEntity entity) => AudioIsPlaying != null && AudioIsPlaying(entity) != 0;
}
