using System.Reflection;
using System.Text.Json.Nodes;

namespace NEMEngine;

// 保存Fieldの型と属性と参照構造を判定する
internal static class ScriptFieldTypeUtility {

    // ネストした共有参照を訪問済みの型で打ち切る
    internal static bool ContainsManagedReference(Type type) {

        return ContainsManagedReference(type, new HashSet<Type>());
    }

#pragma warning disable SYSLIB0050
    // ネストしたFieldも含めて参照型を検出する
    internal static bool IsDeferredReferenceType(Type type, HashSet<Type>? visited) {

        if (type == typeof(GameObject) || typeof(Component).IsAssignableFrom(type)) {
            return true;
        }
        if (type.IsArray) {
            return IsDeferredReferenceType(type.GetElementType()!, visited);
        }
        if (type.IsGenericType) {
            Type def = type.GetGenericTypeDefinition();
            if (def == typeof(List<>) || def == typeof(Nullable<>)) {
                return IsDeferredReferenceType(type.GetGenericArguments()[0], visited);
            }
        }
        // 保存対象のメンバを辿り訪問済みの型で打ち切る
        if (IsSerializableObjectType(type)) {
            visited ??= new HashSet<Type>();
            if (!visited.Add(type)) {
                return false;
            }
            foreach (FieldInfo field in EnumerateNestedSerializedFields(type)) {
                if (IsDeferredReferenceType(field.FieldType, visited)) {
                    return true;
                }
            }
        }
        return false;
    }

    // 値として保存するclassと構造体を判定する
    internal static bool IsSerializableObjectType(Type type) {

        if (!HasSerializableFlag(type) || type.IsPrimitive || type.IsEnum || type == typeof(string) ||
            type.IsAbstract || type.IsGenericType) {
            return false;
        }
        if (!type.IsClass && !type.IsValueType) {
            return false;
        }
        // エンジンの参照型階層とBCL型は対象外
        return !typeof(Object).IsAssignableFrom(type) && type.Assembly != typeof(object).Assembly;
    }

    // 保存対象を示す型属性を取得する
    internal static bool HasSerializableFlag(Type type) {

        return (type.Attributes & TypeAttributes.Serializable) != 0;
    }

    // 継承元を含めて保存対象のFieldを列挙する
    internal static IEnumerable<FieldInfo> EnumerateNestedSerializedFields(Type type,
        bool includePrivate = false, Type? stopType = null) {

        const BindingFlags flags = BindingFlags.Instance | BindingFlags.Public |
            BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
        for (Type? t = type; t != null && t != typeof(object) && t != stopType; t = t.BaseType) {
            foreach (FieldInfo field in t.GetFields(flags)) {
                if (field.IsStatic || field.IsInitOnly || field.IsLiteral ||
                    field.GetCustomAttribute<NonSerializedAttribute>() != null) {
                    continue;
                }
                if (includePrivate || field.IsPublic || field.GetCustomAttribute<SerializeFieldAttribute>() != null ||
                    field.GetCustomAttribute<SerializeReferenceAttribute>() != null) {
                    if (IsSupportedFieldType(field.FieldType,
                        field.GetCustomAttribute<SerializeReferenceAttribute>() != null)) {
                        yield return field;
                    }
                }
            }
        }
    }

    // 保存可能な型と一重の配列だけを対象にする
    internal static bool IsSupportedFieldType(Type type, bool managedReference, bool allowCollection = true) {

        if (type.IsArray) {
            return allowCollection && type.GetArrayRank() == 1 &&
                IsSupportedFieldType(type.GetElementType()!, managedReference, false);
        }
        if (type.IsGenericType && type.GetGenericTypeDefinition() == typeof(List<>)) {
            return allowCollection && IsSupportedFieldType(type.GetGenericArguments()[0], managedReference, false);
        }
        if (managedReference) {
            return !type.IsGenericType && type != typeof(string) && !typeof(Object).IsAssignableFrom(type) &&
                (type.IsClass || type.IsInterface);
        }
        if (type.IsEnum) {
            return Enum.GetUnderlyingType(type) != typeof(long) && Enum.GetUnderlyingType(type) != typeof(ulong);
        }
        if (type == typeof(string) || type == typeof(bool) || type == typeof(byte) || type == typeof(sbyte) ||
            type == typeof(short) || type == typeof(ushort) || type == typeof(int) || type == typeof(uint) ||
            type == typeof(long) || type == typeof(ulong) ||
            type == typeof(float) || type == typeof(double)) {
            return true;
        }
        return type == typeof(Vector2) || type == typeof(Vector3) || type == typeof(Vector4) || type == typeof(Quaternion) ||
            type == typeof(Color3) || type == typeof(Color4) || typeof(Object).IsAssignableFrom(type) ||
            IsSerializableObjectType(type);
    }

    // Inspectorへ表示するFieldを判定する
    internal static bool CanReadRuntimeField(JsonObject field) {

        return !IsUnsupportedField(field) &&
            field["isHidden"]?.GetValue<bool>() != true;
    }

    // 未対応の保存型を判定する
    internal static bool IsUnsupportedField(JsonObject field) {

        return string.Equals(field["kind"]?.GetValue<string>(), "Unsupported", StringComparison.OrdinalIgnoreCase);
    }
#pragma warning restore SYSLIB0050

    // ネストした参照属性も単一Field編集の対象判定に含める
    private static bool ContainsManagedReference(Type type, HashSet<Type> visited) {

        if (!visited.Add(type)) {
            return false;
        }
        if (type.IsArray) {
            return ContainsManagedReference(type.GetElementType()!, visited);
        }
        if (type.IsGenericType && type.GetGenericTypeDefinition() == typeof(List<>)) {
            return ContainsManagedReference(type.GetGenericArguments()[0], visited);
        }
        return IsSerializableObjectType(type) && EnumerateNestedSerializedFields(type).Any(field =>
            field.GetCustomAttribute<SerializeReferenceAttribute>() is not null ||
            ContainsManagedReference(field.FieldType, visited));
    }
}
