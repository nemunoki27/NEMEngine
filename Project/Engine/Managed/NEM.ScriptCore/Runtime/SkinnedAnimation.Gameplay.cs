namespace NEMEngine;

// 自動生成される SkinnedAnimation wrapper の gameplay method 拡張（生成ファイルは編集しない）。
// 既存 SkinnedAnimationSystem の clip 遷移ステートマシンを利用する:
//   clip を設定すると runtimeCurrentClip への遷移が始まり、enabled が time advance を gate する。
// state 名は呼び出し時のみ設定/取得し、毎フレームの文字列 scan はしない。
public sealed partial class SkinnedAnimation {

    // 指定 state(clip) へ遷移再生する。
    public void Play(string stateName) {
        Clip = stateName;
        Enabled = true;
    }

    // 再生を止める（time advance を停止。clip 状態は保持）。
    public void Stop() {
        Enabled = false;
    }

    // 再生中か（有効かつ非ループの自然終了に達していない）。
    public bool IsPlaying => Enabled && !Finished;

    // 指定クリップの再生合計時間(秒)を返す。未ロードや未検出は0。
    public float GetDuration(string clipName) => NativeApi.ReadSkinnedAnimationDuration(entity.native, clipName);

    // CurrentState は生成プロパティ（runtimeCurrentClip / ReadOnly）を利用する。
}
