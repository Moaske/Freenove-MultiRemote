namespace MultiRemoteTray;

// A plain text log in %LOCALAPPDATA%\MultiRemote\tray.log, opened from the
// tray menu. Kept short: it starts again when it passes 1 MB.
static class Log
{
    public static readonly string Folder =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "MultiRemote");
    public static readonly string File = Path.Combine(Folder, "tray.log");
    static readonly object Gate = new();

    public static void Write(string msg)
    {
        try
        {
            lock (Gate)
            {
                Directory.CreateDirectory(Folder);
                var fi = new FileInfo(File);
                if (fi.Exists && fi.Length > 1_000_000) fi.Delete();
                System.IO.File.AppendAllText(File, $"{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}  {msg}{Environment.NewLine}");
            }
        }
        catch { /* logging must never break the app */ }
    }

    public static void Error(string where, Exception ex) =>
        Write($"ERROR in {where}: {ex.GetType().Name}: {ex.Message} (0x{ex.HResult:X8})");
}
