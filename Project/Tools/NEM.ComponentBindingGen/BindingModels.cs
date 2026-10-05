namespace NEM.ComponentBindingGen;

// 列挙値の名前と数値を保持する
internal sealed class EnumMember {

    public string Name = "";
    public long Value;
}

// NativeとManagedで共有する列挙型を表す
internal sealed class EnumModel {

    public string ManagedType = "";
    public string NativeType = "";
    public List<EnumMember> Members = new();
}

// 公開プロパティの型とアクセス方法を表す
internal sealed class PropertyModel {

    public string ManagedName = "";
    public string NativeMember = "";
    public string Kind = "";
    public string? AssetType;
    public string? EnumType;
    public string Access = "ReadWrite";
    public bool ReadOnly => string.Equals(Access, "ReadOnly", StringComparison.Ordinal);
    // 形状別のアクセサから呼ぶプロパティは内部公開にする
    public string Visibility = "Public";
    public string CSVisibility => string.Equals(Visibility, "Internal", StringComparison.Ordinal) ? "internal" : "public";
}

// Componentの登録情報と公開プロパティを表す
internal sealed class ComponentModel {

    public int ID = -1;
    public string RegistryName = "";
    public string NativeType = "";
    public string NativeHeader = "";
    public string Exposure = "";
    public string ManagedType = "";
    public bool ManagedFactory;
    public bool AllowAdd = true;
    public bool AllowRemove = true;
    public List<PropertyModel> Properties = new();
}

// NativeとManagedを接続する関数型を表す
internal sealed class ABIFieldModel {

    public string Name = "";
    public string NativeType = "";
    public string ManagedType = "";
}

// 共有構造体のサイズとメンバー位置を表す
internal sealed class ABILayoutModel {
    public string NativeType = "";
    public string ManagedType = "";
    public int Size;
    public Dictionary<string, int> Members = new();
}

