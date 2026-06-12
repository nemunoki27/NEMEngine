namespace NEMEngine;

// C++ / C# 境界処理の結果コード。値はC++側 ManagedStatus と一致させる
public enum ManagedStatus {

    Ok = 0,
    InvalidArgument,
    InvalidWorldHandle,
    InvalidEntityHandle,
    InvalidInstanceHandle,
    AbiMismatch,
    Unsupported,
    SerializationError,
    ScriptException,
    InternalError,
    // 二段階 blob API で呼び出し側 buffer が不足（必要 size を取得し直して再試行する）
    BufferTooSmall,
}
