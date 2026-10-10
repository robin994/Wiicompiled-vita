using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using Translator.Core.Loading;

namespace Translator.Core.IO;

internal sealed record AssemblyBlob(string FileName, string Symbol, ReadOnlyMemory<byte> Data, string Comment);

internal static class AssemblyBlobWriter
{
    public static void Write(
        string assemblyPath,
        string blobDirectory,
        string blobReferenceDirectory,
        IReadOnlyList<AssemblyBlob> blobs,
        params string[] headerLines)
    {
        Directory.CreateDirectory(blobDirectory);
        var expectedFiles = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var assembly = new StringBuilder();
        foreach (var header in headerLines) assembly.AppendLine(header);
        // .S is preprocessed by the target compiler, so cross builds select the
        // target object format and symbol ABI instead of the translator's host.
        assembly.AppendLine("#if defined(__APPLE__)");
        assembly.AppendLine("#define WIC_BLOB_SYMBOL(name) _##name");
        assembly.AppendLine(".section __TEXT,__const");
        assembly.AppendLine("#elif defined(_WIN32)");
        assembly.AppendLine("#define WIC_BLOB_SYMBOL(name) name");
        assembly.AppendLine(".section .rdata,\"dr\"");
        assembly.AppendLine("#else");
        assembly.AppendLine("#define WIC_BLOB_SYMBOL(name) name");
        assembly.AppendLine("#if defined(__arm__)");
        assembly.AppendLine(".section .rodata,\"a\",%progbits");
        assembly.AppendLine("#else");
        assembly.AppendLine(".section .rodata,\"a\",@progbits");
        assembly.AppendLine("#endif");
        assembly.AppendLine("#endif");
        assembly.AppendLine();

        foreach (var blob in blobs)
        {
            var blobPath = Path.Combine(blobDirectory, blob.FileName);
            expectedFiles.Add(Path.GetFullPath(blobPath));
            FileOutput.WriteBytesIfChanged(blobPath, blob.Data.Span);
            var hash = ChecksumUtilities.Sha256Hex(blob.Data.Span);
            assembly.AppendLine($"// {blob.Comment}; sha256={hash}");
            assembly.AppendLine(".p2align 4");
            // C/C++ external symbols carry a leading underscore in Mach-O,
            // unlike ELF and COFF.  The generated C++ still names the symbol
            // without that ABI decoration, so emit the platform spelling here.
            var assemblySymbol = $"WIC_BLOB_SYMBOL({blob.Symbol})";
            assembly.AppendLine($".globl {assemblySymbol}");
            assembly.AppendLine($"{assemblySymbol}:");
            var referencePath = Path.Combine(blobReferenceDirectory, blob.FileName);
            assembly.AppendLine($".incbin \"{SanitizeAssemblyPath(referencePath)}\"");
            assembly.AppendLine();
        }

        foreach (var stalePath in Directory.EnumerateFiles(blobDirectory, "*.bin"))
        {
            if (!expectedFiles.Contains(Path.GetFullPath(stalePath))) File.Delete(stalePath);
        }
        assembly.AppendLine("#if !defined(_WIN32) && !defined(__APPLE__)");
            // Absence of a .note.GNU-stack section makes the linker assume the oldest, most
            // conservative default for this object (an executable stack) and warn about it; this
            // file has no code needing one, so mark it explicitly like every other GNU-as ELF
            // object linked into the binary already does (the norm on modern toolchains, just not
            // producible without an explicit section since this file is hand-assembled, not
            // compiler-emitted).
        assembly.AppendLine("#if defined(__arm__)");
        assembly.AppendLine(".section .note.GNU-stack,\"\",%progbits");
        assembly.AppendLine("#else");
        assembly.AppendLine(".section .note.GNU-stack,\"\",@progbits");
        assembly.AppendLine("#endif");
        assembly.AppendLine("#endif");
        FileOutput.WriteTextIfChanged(assemblyPath, assembly.ToString());
    }

    private static string SanitizeAssemblyPath(string path) => Path.GetFullPath(path).Replace('\\', '/');
}
