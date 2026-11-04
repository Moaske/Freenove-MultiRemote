// MultiRemote tray app
//
// Sits in the Windows notification area. Reads what is playing from Windows
// (the same information as the volume overlay: Spotify, browsers, foobar2000,
// Windows Media Player...) and sends title, artist, album, playing/paused and
// a 60x60 cover to the remote over Bluetooth LE.
//
// The remote must be paired with this PC first (Settings > Bluetooth &
// devices > Add device). Media keys work without this app; it only adds the
// track info.
namespace MultiRemoteTray;

static class Program
{
    [STAThread]
    static void Main()
    {
        using var single = new Mutex(true, "MultiRemoteTray.SingleInstance", out bool first);
        if (!first) return;

        ApplicationConfiguration.Initialize();
        Application.Run(new TrayContext());
    }
}
