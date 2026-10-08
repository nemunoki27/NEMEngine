namespace NEM.ComponentBindingGen;

// 列挙値の名前と数値を保持する
internal sealed class EnumMember {

    public string Name = ""; // 列挙値名
    public long Value; // 列挙値
}

// NativeとManagedで共有する列挙型を表す
internal sealed class EnumModel {

    public string ManagedType = ""; // Managed型名
    public string NativeType = ""; // Native型名
    public List<EnumMember> Members = new(); // 列挙値の一覧
}

// 公開プロパティの型とアクセス方法を表す
internal sealed class PropertyModel {

    public string ManagedName = ""; // 公開プロパティ名
    public string NativeMember = ""; // 対応するNativeメンバー
    public string Kind = ""; // 値の種別
    public string? AssetType; // Asset参照の型
    public string? EnumType; // 列挙型名
    public string Access = "ReadWrite"; // 読取と書込の許可
    // 形状別のアクセサから呼ぶプロパティは内部公開にする
    public string Visibility = "Public"; // 公開範囲

    //--------- accessor -----------------------------------------------------

    // 読取専用かを取得する
    public bool ReadOnly => string.Equals(Access, "ReadOnly", StringComparison.Ordinal);
    // Managed宣言の公開範囲を取得する
    public string CSVisibility => string.Equals(Visibility, "Internal", StringComparison.Ordinal) ? "internal" : "public";
}

// Componentの登録情報と公開プロパティを表す
internal sealed class ComponentModel {

    public int ID = -1; // 固定の型番号
    public string RegistryName = ""; // 登録名
    public string NativeType = ""; // Native型名
    public string NativeHeader = ""; // Native型の宣言先
    public string Exposure = ""; // 接続方式
    public string ManagedType = ""; // Managed型名
    public bool ManagedFactory; // 手書き型の生成対応
    public bool AllowAdd = true; // Component追加の許可
    public bool AllowRemove = true; // Component削除の許可
    public List<PropertyModel> Properties = new(); // 公開プロパティ
}

// NativeとManagedを接続する関数型を表す
internal sealed class ABIFieldModel {

    public string Name = ""; // 接続関数名
    public string NativeType = ""; // Native関数型
    public string ManagedType = ""; // Managed関数型
}

// 共有構造体のサイズとメンバー位置を表す
internal sealed class ABILayoutModel {

    public string NativeType = ""; // Native型名
    public string ManagedType = ""; // Managed型名
    public int Size; // 構造体のサイズ
    public Dictionary<string, int> Members = new(); // メンバーの位置
}

