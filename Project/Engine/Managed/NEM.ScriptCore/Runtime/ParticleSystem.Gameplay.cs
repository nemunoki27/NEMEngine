namespace NEMEngine;

// ParticleSystem.Stopで既存粒子を残すか破棄するか
public enum ParticleSystemStopBehavior {

    StopEmittingAndClear = 0,
    StopEmitting = 1,
}

// 自動生成されるParticleSystemへUnityと同じ基本再生APIを追加する
public sealed partial class ParticleSystem {

    private const int PlayOperation = 0;
    private const int PauseOperation = 1;
    private const int StopOperation = 2;
    private const int ClearOperation = 3;
    private const int PlayOneShotOperation = 4;

    private const int PlayingState = 0;
    private const int EmittingState = 1;
    private const int PausedState = 2;
    private const int StoppedState = 3;
    private const int AliveState = 4;
    private const int ParticleCountState = 5;

    // 停止中は先頭から再生し、一時停止中は続きから再開する
    public void Play(bool withChildren = true) =>
        NativeAPI.ParticleSystemControlCall(entity.native, PlayOperation,
            ParticleSystemStopBehavior.StopEmitting, withChildren);

    // 現在の再生を先頭へ戻し、ループ設定を無視して1回だけ再生する
    public void PlayOneShot(bool withChildren = true) =>
        NativeAPI.ParticleSystemControlCall(entity.native, PlayOneShotOperation,
            ParticleSystemStopBehavior.StopEmitting, withChildren);

    // 現在位置を保持して更新を一時停止する
    public void Pause(bool withChildren = true) =>
        NativeAPI.ParticleSystemControlCall(entity.native, PauseOperation,
            ParticleSystemStopBehavior.StopEmitting, withChildren);

    // 発生だけを止めるか、既存粒子も同時に消して停止する
    public void Stop(bool withChildren = true,
        ParticleSystemStopBehavior stopBehavior =
            ParticleSystemStopBehavior.StopEmitting) =>
        NativeAPI.ParticleSystemControlCall(entity.native, StopOperation,
            stopBehavior, withChildren);

    // 再生状態を変えずに既存粒子をすべて消す
    public void Clear(bool withChildren = true) =>
        NativeAPI.ParticleSystemControlCall(entity.native, ClearOperation,
            ParticleSystemStopBehavior.StopEmitting, withChildren);

    public bool isPlaying =>
        NativeAPI.ParticleSystemStateCall(entity.native, PlayingState) != 0;
    public bool isEmitting =>
        NativeAPI.ParticleSystemStateCall(entity.native, EmittingState) != 0;
    public bool isPaused =>
        NativeAPI.ParticleSystemStateCall(entity.native, PausedState) != 0;
    public bool isStopped =>
        NativeAPI.ParticleSystemStateCall(entity.native, StoppedState) != 0;
    public int particleCount =>
        NativeAPI.ParticleSystemStateCall(entity.native, ParticleCountState);

    // 子階層を含めて生存粒子または再生中のSystemがあるか返す
    public bool IsAlive(bool withChildren = true) =>
        NativeAPI.ParticleSystemStateCall(
            entity.native, AliveState, withChildren) != 0;
}
