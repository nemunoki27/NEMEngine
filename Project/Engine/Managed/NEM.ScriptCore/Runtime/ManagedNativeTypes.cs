using System.Runtime.InteropServices;

namespace NEMEngine;

// Nativeと共通のABI定数
internal static class ManagedABI {

    // NativeとManagedを同じ版で接続する
    internal const uint Version = 67;

    // ネイティブが提供する機能カテゴリ
    internal const ulong CapabilityCore = 1ul << 0;
    internal const ulong CapabilityInput = 1ul << 1;
    internal const ulong CapabilityEntity = 1ul << 2;
    internal const ulong CapabilityHierarchy = 1ul << 3;
    internal const ulong CapabilityTransform = 1ul << 4;
    internal const ulong CapabilityObjectModel = 1ul << 5;
    internal const ulong CapabilityComponentBindings = 1ul << 6;
    internal const ulong CapabilityGameplay = 1ul << 7;

    // ScriptCoreが必要とする機能
    internal const ulong RequiredCapabilities =
        CapabilityCore | CapabilityInput | CapabilityEntity | CapabilityHierarchy | CapabilityTransform
        | CapabilityObjectModel | CapabilityComponentBindings | CapabilityGameplay;
}

// Nativeと共通のRenderer種別
internal enum RendererMaterialTarget {

    Mesh = 0,
    Sprite,
    Text,
    Primitive,
    Line,
}

// Nativeと共通のMaterial値種別
internal enum NativeMaterialParameterValueType {

    Float = 0,
    Vector2,
    Vector3,
    Vector4,
    Color,
    Texture,
    Int,
    UInt,
    Bool,
}

// Nativeと共通のMaterial値配置
[StructLayout(LayoutKind.Explicit, Size = 24)]
public struct NativeMaterialParameterValue {

    [FieldOffset(0)] internal ulong data0;
    [FieldOffset(8)] internal ulong data1;
    [FieldOffset(0)] internal float x;
    [FieldOffset(4)] internal float y;
    [FieldOffset(8)] internal float z;
    [FieldOffset(12)] internal float w;
    [FieldOffset(0)] internal int intValue;
    [FieldOffset(0)] internal uint uintValue;
    [FieldOffset(0)] internal AssetGUID assetID;
    [FieldOffset(16)] internal NativeMaterialParameterValueType type;
    [FieldOffset(20)] internal int reserved;
}

// Nativeと共通のABI情報配置
[StructLayout(LayoutKind.Sequential)]
public struct ManagedABIHeader {

    public uint abiVersion;
    public uint structSize;
    public ulong capabilities;
    public ulong bindingFingerprint;
}

// Nativeと共通のScript世代付き参照
[StructLayout(LayoutKind.Sequential)]
public readonly struct NativeScriptInstanceHandle {

    public readonly uint index;
    public readonly uint generation;

    public NativeScriptInstanceHandle(uint index, uint generation) {
        this.index = index;
        this.generation = generation;
    }

    // 既定値の世代0を無効として扱う
    public bool IsValid => index != 0xffffffffu && generation != 0;
    public static NativeScriptInstanceHandle Null => new(0xffffffffu, 0);
}

// Nativeと共通の3軸値
[StructLayout(LayoutKind.Sequential)]
public struct NativeVector3 {

    public float x;
    public float y;
    public float z;

    public static NativeVector3 From(Vector3 value) {
        return new NativeVector3 {
            x = value.x,
            y = value.y,
            z = value.z
        };
    }

    public Vector3 ToVector3() {
        return new Vector3(x, y, z);
    }
}

// Nativeと共通の2軸値
[StructLayout(LayoutKind.Sequential)]
public struct NativeVector2 {

    public float x;
    public float y;

    public static NativeVector2 From(Vector2 value) {
        return new NativeVector2 {
            x = value.x,
            y = value.y
        };
    }

    public Vector2 ToVector2() {
        return new Vector2(x, y);
    }
}

// Nativeと共通のレイ接触結果
[StructLayout(LayoutKind.Sequential)]
public struct NativeRaycastHit {

    public NativeEntity entity;
    public NativeVector3 point;
    public NativeVector3 normal;
    public float distance;
    public int shapeIndex;
    public int trigger;
}

// Nativeと共通のAnimation再生状態
[StructLayout(LayoutKind.Sequential)]
public struct NativeSkinnedAnimationRuntimeState {

    public float currentTime;
    public float currentDuration;
    public float blendTime;
    public int repeatCount;
    public int initialized;
    public int finished;
    public int inTransition;
}

// Nativeと共通のUI選択状態
[StructLayout(LayoutKind.Sequential)]
public struct NativeUISelectableRuntimeState {

    public int state;
    public int normalThisFrame;
    public int selectedThisFrame;
    public int submittedThisFrame;
    public int disabledThisFrame;
}

// Nativeと共通のUI表示値
[StructLayout(LayoutKind.Sequential)]
public struct NativeUIProgressRuntimeState {

    public float displayedValue;
    public float delayedValue;
    public int initialized;
}

// Nativeと共通の色値
[StructLayout(LayoutKind.Sequential)]
public struct NativeColor4 {

    public float r;
    public float g;
    public float b;
    public float a;

    public static NativeColor4 From(Color4 value) {
        return new NativeColor4 {
            r = value.r,
            g = value.g,
            b = value.b,
            a = value.a
        };
    }

    public Color4 ToColor4() {
        return new Color4(r, g, b, a);
    }
}

// Nativeと共通の回転値
[StructLayout(LayoutKind.Sequential)]
public struct NativeQuaternion {

    public float x;
    public float y;
    public float z;
    public float w;

    public static NativeQuaternion From(Quaternion value) {
        return new NativeQuaternion {
            x = value.x,
            y = value.y,
            z = value.z,
            w = value.w
        };
    }

    public Quaternion ToQuaternion() {
        return new Quaternion(x, y, z, w);
    }
}

// Nativeと共通の即時描画形状
public enum LineShapeType {

    Circle2D = 0,
    Rect2D,
    Hemisphere,
    AABB,
    OBB,
    Cone,
    Arrow,
    Axis,
}

// Materialの8バイト境界を保つNative形状配置
[StructLayout(LayoutKind.Sequential)]
public struct NativeLineShape {

    public AssetGUID materialID;
    public int shapeType;
    public int division;
    public int is2D;
    public float radius;
    public float radius2;
    public float height;
    public float thickness;
    public NativeVector3 a;
    public NativeVector3 b;
    public NativeQuaternion rotation;
    public NativeColor4 color;
}

//============================================================================
//	NativeScriptTypeInfo structure
//	NativeのScript型情報と配置を揃える
//============================================================================
[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeScriptTypeInfo {

    // 正規化済みScript型ID
    public fixed byte scriptTypeID[40];
    // 完全修飾型名
    public fixed byte fullTypeName[256];
    // 表示名
    public fixed byte displayName[128];
    // 定義元のソース位置
    public fixed byte sourcePath[260];
    // Script型IDの明示指定
    public int hasExplicitID;
    // 既定の実行順序
    public int defaultExecutionOrder;
}

//============================================================================
//	NativeCollisionEvent structure
//============================================================================
[StructLayout(LayoutKind.Sequential)]
public struct NativeCollisionEvent {

    // コールバックを受け取るGameObjectと相手GameObject
    public NativeEntity self;
    public NativeEntity other;
    // 接触情報
    public NativeVector3 normal;
    public NativeVector3 point;
    public float penetration;
    // 衝突した形状インデックス
    public int selfShapeIndex;
    public int otherShapeIndex;
    // Trigger接触なら1
    public int isTrigger;
}
