using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;
using Windows.Media.Control;

namespace MultiRemoteTray;

enum PlayState { None, Playing, Paused }

sealed record NowPlaying(PlayState State, string Title, string Artist, string Album, byte[]? Art)
{
    public static readonly NowPlaying Nothing = new(PlayState.None, "", "", "", null);
}

// What Windows says is playing (System Media Transport Controls).
sealed class MediaWatcher
{
    public const int ArtSize = 60;

    GlobalSystemMediaTransportControlsSessionManager? _mgr;
    GlobalSystemMediaTransportControlsSession? _session;

    public event Action? Changed;

    public async Task StartAsync()
    {
        _mgr = await GlobalSystemMediaTransportControlsSessionManager.RequestAsync();
        _mgr.CurrentSessionChanged += (_, _) => Hook();
        Hook();
    }

    void Hook()
    {
        if (_session != null)
        {
            _session.MediaPropertiesChanged -= OnMedia;
            _session.PlaybackInfoChanged -= OnPlayback;
        }
        _session = _mgr?.GetCurrentSession();
        if (_session != null)
        {
            _session.MediaPropertiesChanged += OnMedia;
            _session.PlaybackInfoChanged += OnPlayback;
        }
        Changed?.Invoke();
    }

    void OnMedia(GlobalSystemMediaTransportControlsSession s, MediaPropertiesChangedEventArgs e) => Changed?.Invoke();
    void OnPlayback(GlobalSystemMediaTransportControlsSession s, PlaybackInfoChangedEventArgs e) => Changed?.Invoke();

    public async Task<NowPlaying> ReadAsync()
    {
        var s = _session;
        if (s == null) return NowPlaying.Nothing;

        var info = s.GetPlaybackInfo();
        var state = info?.PlaybackStatus switch
        {
            GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing => PlayState.Playing,
            GlobalSystemMediaTransportControlsSessionPlaybackStatus.Paused => PlayState.Paused,
            _ => PlayState.None,
        };

        GlobalSystemMediaTransportControlsSessionMediaProperties props;
        try { props = await s.TryGetMediaPropertiesAsync(); }
        catch { return NowPlaying.Nothing; }

        var artist = string.IsNullOrWhiteSpace(props.Artist) ? props.AlbumArtist : props.Artist;
        byte[]? art = null;
        if (props.Thumbnail != null)
        {
            try { art = await ReadArtAsync(props.Thumbnail); }
            catch { art = null; }   // some players hand out a broken thumbnail
        }

        if (string.IsNullOrWhiteSpace(props.Title)) state = PlayState.None;
        return new NowPlaying(state, Ascii(props.Title), Ascii(artist), Ascii(props.AlbumTitle), art);
    }

    // The cover, centre-cropped to a square, scaled to 60x60, as RGB565
    // little-endian -- exactly what the remote pushes to its screen.
    static async Task<byte[]> ReadArtAsync(Windows.Storage.Streams.IRandomAccessStreamReference thumb)
    {
        using var ras = await thumb.OpenReadAsync();
        using var ms = new MemoryStream();
        await ras.AsStreamForRead().CopyToAsync(ms);
        ms.Position = 0;
        using var src = Image.FromStream(ms);

        int side = Math.Min(src.Width, src.Height);
        var crop = new Rectangle((src.Width - side) / 2, (src.Height - side) / 2, side, side);

        using var bmp = new Bitmap(ArtSize, ArtSize, PixelFormat.Format24bppRgb);
        using (var g = Graphics.FromImage(bmp))
        {
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            g.PixelOffsetMode = PixelOffsetMode.HighQuality;
            g.DrawImage(src, new Rectangle(0, 0, ArtSize, ArtSize), crop, GraphicsUnit.Pixel);
        }

        var data = bmp.LockBits(new Rectangle(0, 0, ArtSize, ArtSize), ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
        var row = new byte[data.Stride];
        var outBuf = new byte[ArtSize * ArtSize * 2];
        int o = 0;
        for (int y = 0; y < ArtSize; y++)
        {
            Marshal.Copy(data.Scan0 + y * data.Stride, row, 0, data.Stride);
            for (int x = 0; x < ArtSize; x++)
            {
                byte b = row[x * 3], gr = row[x * 3 + 1], r = row[x * 3 + 2];
                ushort c = (ushort)(((r & 0xF8) << 8) | ((gr & 0xFC) << 3) | (b >> 3));
                outBuf[o++] = (byte)(c & 0xFF);
                outBuf[o++] = (byte)(c >> 8);
            }
        }
        bmp.UnlockBits(data);
        return outBuf;
    }

    // The remote's fonts are plain ASCII: "Björk - Jóga" becomes "Bjork - Joga".
    public static string Ascii(string? s, int max = 100)
    {
        if (string.IsNullOrWhiteSpace(s)) return "";
        var sb = new StringBuilder();
        foreach (var ch in s.Normalize(NormalizationForm.FormD))
        {
            if (CharUnicodeInfo.GetUnicodeCategory(ch) == UnicodeCategory.NonSpacingMark) continue;
            switch (ch)
            {
                case >= ' ' and < (char)0x7F: sb.Append(ch); break;
                case '‘' or '’' or '´': sb.Append('\''); break;
                case '“' or '”': sb.Append('"'); break;
                case '–' or '—': sb.Append('-'); break;
                case '…': sb.Append("..."); break;
                case 'ß': sb.Append("ss"); break;
                case 'æ': sb.Append("ae"); break;
                case 'Æ': sb.Append("AE"); break;
                case 'ø': sb.Append('o'); break;
                case 'Ø': sb.Append('O'); break;
                case 'œ': sb.Append("oe"); break;
                default:
                    if (char.IsWhiteSpace(ch)) sb.Append(' ');
                    else if (!char.IsControl(ch)) sb.Append('?');
                    break;
            }
            if (sb.Length >= max) break;
        }
        return sb.ToString().Trim();
    }
}
