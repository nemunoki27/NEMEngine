namespace NEMEngine;

// 自動生成されるAudioSource wrapperのgameplay method拡張
// 実際のvoice制御はAudioSourceSystemが再生要求を順番に処理する
public sealed partial class AudioSource {

    // Clipを主再生として先頭から再生
    public void Play() => NativePlaybackAPI.AudioPlayCall(entity.native);
    // 指定Clipを重ねて一度だけ再生
    public void PlayOneShot(AudioClip clip, float volumeScale = 1.0f) {
        if (clip == null) {
            return;
        }
        NativePlaybackAPI.AudioPlayOneShotCall(entity.native, clip.id, volumeScale);
    }
    // 再生位置を保持して一時停止
    public void Pause() => NativePlaybackAPI.AudioPauseCall(entity.native);
    // 一時停止中の再生を再開
    public void UnPause() => NativePlaybackAPI.AudioUnPauseCall(entity.native);
    // 停止（voice 破棄）
    public void Stop() => NativePlaybackAPI.AudioStopCall(entity.native);
    // 再生中か（pause 中は false）
    public bool IsPlaying => NativePlaybackAPI.AudioIsPlayingCall(entity.native);
}
