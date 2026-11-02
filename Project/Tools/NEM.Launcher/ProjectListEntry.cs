using System.ComponentModel;
using NEMEngine.Distribution;

namespace NEMEngine.Launcher;

internal sealed class ProjectListEntry(ProjectEntry project) : INotifyPropertyChanged {
    public ProjectEntry Project => project;
    public string Name => project.Name;
    public string Descriptor => project.Descriptor;
    public string EngineVersion => project.EngineVersion;
    public string Status => project.Status;
    public DateTime LastOpened => project.LastOpened;
    public string GitWebURL { get; private set; } = "";
    public bool HasGitRepository => GitWebURL.Length != 0;
    public event PropertyChangedEventHandler? PropertyChanged;

    public void SetGitLink(string url) {
        GitWebURL = url;
        PropertyChanged?.Invoke(this, new(nameof(GitWebURL)));
        PropertyChanged?.Invoke(this, new(nameof(HasGitRepository)));
    }
}
