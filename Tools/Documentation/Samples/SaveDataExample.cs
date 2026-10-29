using NEMEngine;
using System.IO;

public sealed class PlayerSaveData {

    public int score;
    public Vector3 position;
}

public sealed class SaveDataExample : MonoBehaviour {

    void Update() {
        // 保存・読込の失敗をGame Consoleへ通知
        try {
            if (Input.GetKeyDown(KeyCode.F5)) Save();
            if (Input.GetKeyDown(KeyCode.F9)) Load();
        } catch (Exception error) {
            Debug.LogWarning($"セーブデータを処理できません: {error.Message}");
        }
    }

    void Save() {
        // 保存したい値をゲーム用のデータへまとめる
        var data = new PlayerSaveData { score = 100, position = transform.position };
        // ユーザーの保存先へJSONを書き出す
        Directory.CreateDirectory(Application.persistentDataPath);
        string path = Path.Combine(Application.persistentDataPath, "save.json");
        File.WriteAllText(path, JsonUtility.ToJson(data));
    }

    void Load() {
        // 保存ファイルがある場合だけ復元
        string path = Path.Combine(Application.persistentDataPath, "save.json");
        if (!File.Exists(path)) return;
        var data = JsonUtility.FromJson<PlayerSaveData>(File.ReadAllText(path));
        transform.position = data.position;
    }
}
