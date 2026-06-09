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
}
