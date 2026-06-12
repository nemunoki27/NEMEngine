namespace NEMEngine;

// 自動生成される AudioSource wrapper の gameplay method 拡張（生成ファイルは編集しない）。
// 実際の voice 制御は AudioSourceSystem が runtimePlayRequest を消費して行う（Play/Pause/Stop は次フレーム反映）。
public readonly partial struct AudioSource {

    // 明示再生（pause 中なら resume、未再生なら clip を再生）
    public void Play() => NativeApi.AudioPlayCall(entity.native);
    // 再生位置を保持して一時停止
    public void Pause() => NativeApi.AudioPauseCall(entity.native);
    // 停止（voice 破棄）
    public void Stop() => NativeApi.AudioStopCall(entity.native);
    // 再生中か（pause 中は false）
    public bool IsPlaying => NativeApi.AudioIsPlayingCall(entity.native);
}
