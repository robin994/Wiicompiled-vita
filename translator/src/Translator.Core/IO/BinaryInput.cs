using System.Collections.Concurrent;
using System.Diagnostics;

namespace Translator.Core.IO;

/// <summary>Ordinary binary paths or iso:path/to/mariokart.iso#/disc/file.
/// ISO inputs remain in memory; the native disc tool shares the runtime's
/// decryption, integrity checks and FST parser instead of extracting assets.</summary>
public static class BinaryInput
{
    private static readonly ConcurrentDictionary<string, byte[]> IsoCache = new();
    public static bool IsIso(string path) => path.StartsWith("iso:", StringComparison.Ordinal);

    public static (string Image, string Entry) Split(string path)
    {
        var split = path.LastIndexOf('#');
        if (!IsIso(path) || split <= 4 || split == path.Length - 1 || path[split + 1] != '/')
            throw new InvalidDataException("ISO input must be iso:path/to/mariokart.iso#/disc/file");
        return (path[4..split], path[(split + 1)..]);
    }

    public static string Resolve(string root, string path)
    {
        if (!IsIso(path)) return Path.GetFullPath(Path.Combine(root, path));
        var (image, entry) = Split(path);
        return "iso:" + Path.GetFullPath(Path.Combine(root, image)) + "#" + entry;
    }

    public static bool Exists(string path) => File.Exists(IsIso(path) ? Split(path).Image : path);

    public static Stream OpenRead(string path)
    {
        if (!IsIso(path)) return File.OpenRead(path);
        var bytes = IsoCache.GetOrAdd(path, ReadIso);
        return new MemoryStream(bytes, writable: false);
    }

    public static byte[] ReadAllBytes(string path)
    {
        using var stream = OpenRead(path);
        using var result = new MemoryStream();
        stream.CopyTo(result);
        return result.ToArray();
    }

    private static byte[] ReadIso(string path)
    {
        var (image, entry) = Split(path);
        var tool = Environment.GetEnvironmentVariable("WIC_DISC_TOOL");
        if (string.IsNullOrWhiteSpace(tool))
            throw new InvalidOperationException("ISO translation requires WIC_DISC_TOOL pointing to wiicompiled-disc.");
        var start = new ProcessStartInfo(tool) {
            UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true
        };
        start.ArgumentList.Add("read"); start.ArgumentList.Add(image); start.ArgumentList.Add(entry);
        using var process = Process.Start(start) ?? throw new IOException("Cannot start the ISO reader");
        var errors = process.StandardError.ReadToEndAsync();
        using var result = new MemoryStream();
        var buffer = new byte[64 * 1024];
        try {
            int count;
            while ((count = process.StandardOutput.BaseStream.Read(buffer)) != 0) {
                if (result.Length + count > 32 * 1024 * 1024)
                    throw new InvalidDataException("ISO translation input exceeds 32 MiB");
                result.Write(buffer, 0, count);
            }
            process.WaitForExit();
            if (process.ExitCode != 0) throw new InvalidDataException(errors.GetAwaiter().GetResult());
            return result.ToArray();
        } catch {
            if (!process.HasExited) process.Kill(entireProcessTree: true);
            throw;
        }
    }
}
