using NEMEngine;

public sealed class SceneFlow : MonoBehaviour {
    [SerializeField] private SceneAsset? primary = null;
    [SerializeField] private SceneAsset? alternate = null;
    [SerializeField] private Prefab? token = null;
    [SerializeField] private AnimationClip? motion = null;
    [SerializeField] private TimelineAsset? sequence = null;
    [SerializeField] private AudioClip? tone = null;

    [SerializeField] private SceneAsset? select = null;

    [SerializeField] private SceneAsset? overlay = null;

    [Serializable]
    private sealed class ItemTable {
        public TextAsset? asset = null;
    }

    private static SceneHandle overlayHandle;
    private static int overlayPhase;
    private static GameObject? traveler;
    private static readonly string[] route = {
        "Title", "Game", "Clear", "Title", "Select", "Game", "Result", "NextStage", "Game", "Select", "Title"
    };
    private static readonly string? report = Environment.GetEnvironmentVariable("NEM_PRODUCT_FLOW_REPORT");
    private static int step;
    private static GameObject? previousScreen;
    private static GameObject? previousToken;
    private GameObject? first;
    private GameObject? second;
    private int frames;
    private bool removed;
    private bool leaving;

    private void Start() {
        try {
            if (gameObject.name == "Title") ProductComponents.Begin(motion, sequence, tone);
            if (report != null && step == 1) {
                if (traveler == null || GameObject.Find("Traveler") != traveler)
                    throw new InvalidOperationException("Persistent object identity changed.");
                NEMEngine.Object.Destroy(traveler);
            }
            if (report != null && step == 2 && traveler != null)
                throw new InvalidOperationException("Persistent object was not destroyed.");
            if (previousScreen != null || previousToken != null)
                throw new InvalidOperationException("A previous scene object is still alive.");
            if (report != null) {
                if (step == 0) {
                    var table = JsonUtility.FromJson<ItemTable>("{\"asset\":{\"assetId\":\"4e454d50524f44464c4f570000000401\"}}");
                    if (table.asset == null || !table.asset.exists || table.asset.text != "id,name\n1,Potion")
                        throw new InvalidDataException("Dynamically selected item table was not loaded from the product.");
                    File.AppendAllText(report, "dynamic asset OK" + Environment.NewLine);
                    string save = Path.Combine(Application.persistentDataPath, "scene-flow.save");
                    bool restart = Environment.GetEnvironmentVariable("NEM_PRODUCT_RESTART") == "1";
                    if (restart && File.ReadAllText(save) != "cleared")
                        throw new InvalidDataException("Progress was lost across restart.");
                    if (!restart && File.Exists(save))
                        throw new InvalidDataException("Expected an isolated first launch.");
                    if (restart) File.AppendAllText(report, "save restored" + Environment.NewLine);
                }
                if (step >= route.Length || gameObject.name != route[step])
                    throw new InvalidOperationException("Unexpected scene " + gameObject.name);
                File.AppendAllText(report, "scene " + gameObject.name + Environment.NewLine);
            }
            if (gameObject.name == "Game") {
                if (token == null || !token.exists) throw new InvalidOperationException("Missing packed prefab.");
                first = token.Instantiate(new Vector3(380, 250, 0), Quaternion.identity);
                second = token.Instantiate(new Vector3(580, 250, 0), Quaternion.identity);
                if (ReferenceEquals(first, null) || ReferenceEquals(second, null) || first == second)
                    throw new InvalidOperationException("Prefab instances did not get distinct handles.");
            }
        } catch (Exception ex) { Fail(ex); }
    }

    private void Update() {
        if (leaving) return;
        ++frames;
        try {
            if (gameObject.name == "Title" && !ProductComponents.Tick()) return;
            if (report != null && step == 0 && !CheckOverlay()) return;
            if (Input.GetKeyDown(KeyCode.Escape)) { Application.Quit(); return; }
            if (gameObject.name == "Game") {
                if (frames == 2) {
                    if (first == null || second == null) throw new InvalidOperationException("Spawn did not commit.");
                    first.name = "Left token";
                    second.name = "Right token";
                    if (GameObject.Find("Left token") != first || GameObject.Find("Right token") != second)
                        throw new InvalidOperationException("Scene lookup returned a different instance.");
                }
                if (frames < 4) return;
                if (!removed) {
                    if (report == null && !Input.GetKeyDown(KeyCode.Space)) return;
                    NEMEngine.Object.Destroy(first);
                    removed = true;
                    return;
                }
                if (first != null || second == null || GameObject.Find("Right token") != second)
                    throw new InvalidOperationException("Removing one token affected the other.");
                if (frames == 6 && report != null) File.AppendAllText(report, "prefab OK" + Environment.NewLine);
            }
            if (frames < 8) return;
            bool other;
            if (report != null) {
                if (step == route.Length - 1) {
                    Directory.CreateDirectory(Application.persistentDataPath);
                    File.WriteAllText(Path.Combine(Application.persistentDataPath, "scene-flow.save"), "cleared");
                    File.AppendAllText(report, "PASS" + Environment.NewLine);
                    leaving = true;
                    Application.Quit();
                    return;
                }
                other = (step == 3 || step == 5 || step == 9);
                if (step == 8) {
                    Load(alternate, selectAfterReplay: true);
                    return;
                }
                ++step;
            } else {
                other = Input.GetKeyDown(KeyCode.Tab);
                if (!other && !Input.GetKeyDown(KeyCode.Return)) return;
                if (gameObject.name == "Game" && Input.GetKey(KeyCode.LeftShift)) {
                    Load(alternate, selectAfterReplay: true);
                    return;
                }
            }
            Load(other ? alternate : primary);
        } catch (Exception ex) { Fail(ex); }
    }


    private bool CheckOverlay() {
        switch (overlayPhase) {
            case 0:
                overlayHandle = SceneManager.LoadAdditive(overlay);
                ++overlayPhase;
                return false;
            case 1:
                if (!overlayHandle.IsValid || GameObject.Find("OverlayMarker") == null) return false;
                if (GameObject.Find("Title") != gameObject || !SceneManager.Unload(overlayHandle))
                    throw new InvalidOperationException("Active scene was lost or unload failed.");
                ++overlayPhase;
                return false;
            case 2:
                if (overlayHandle.IsValid || GameObject.Find("OverlayMarker") != null) return false;
                traveler = new GameObject("Traveler");
                if (!SceneManager.DontDestroyOnLoad(traveler))
                    throw new InvalidOperationException("Persistent object registration failed.");
                File.AppendAllText(report!, "additive OK" + Environment.NewLine);
                ++overlayPhase;
                break;
        }
        return true;
    }

    private void Load(SceneAsset? scene, bool selectAfterReplay = false) {
        if (selectAfterReplay) {
            scene = select;
            if (report != null) ++step;
        }
        if (scene == null || !scene.exists) throw new InvalidOperationException("Missing target from " + gameObject.name);
        previousScreen = gameObject;
        previousToken = second;
        leaving = true;
        SceneManager.LoadScene(scene);
    }

    private void Fail(Exception error) {
        leaving = true;
        if (report != null) File.AppendAllText(report, error.ToString() + Environment.NewLine);
        Console.Error.WriteLine(error);
        Application.Quit();
    }
}
