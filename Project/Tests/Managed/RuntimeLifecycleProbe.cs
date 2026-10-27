using NEMEngine;

namespace NEMTests.Scripts;

//============================================================================
//	RuntimeLifecycleProbe
//============================================================================
[ScriptTypeID("8f07344d-0f75-4a75-bc1b-f9f5f203811c")]
public sealed class RuntimeLifecycleProbe : MonoBehaviour {

    [SerializeField, SerializedFieldID("6ae09a0e-2e17-4405-ae7c-2f39a2d7d39b")]
    private int mask;
    [SerializeField, SerializedFieldID("bda98a95-8333-4c86-b4d1-0320f6e3032a")]
    private float value;
    [SerializeField, SerializedFieldID("9b7d3d9c-34cd-4898-b540-5dfd6cc1e243")]
    private bool throwInAwake;
    [SerializeField, SerializedFieldID("20da6d5a-9d30-4fde-a6a9-b37f9cf3b4ba")]
    private bool throwInUpdate;
    [SerializeField, SerializedFieldID("f1880865-e87f-4b96-985a-6639a1158053")]
    private int updates;

    private void Awake() {

        // 初回だけ例外を発生させて再開地点を確認する
        mask |= 1;
        if (throwInAwake) {
            throwInAwake = false;
            throw new InvalidOperationException("Awakeの検証用例外");
        }
    }

    private void OnEnable() {

        mask |= 2;
    }

    private void Start() {

        mask |= 4;
    }

    private void Update() {

        updates++;
        if (throwInUpdate) {
            throw new InvalidOperationException("Updateの検証用例外");
        }
    }
}
