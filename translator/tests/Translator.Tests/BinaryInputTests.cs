using Translator.Core.IO;
using Xunit;

namespace Translator.Tests;

public class BinaryInputTests
{
    [Theory]
    [InlineData("iso:#/sys/main.dol")]
    [InlineData("iso:game.iso#")]
    [InlineData("iso:game.iso#main.dol")]
    [InlineData("game.iso#/sys/main.dol")]
    public void RejectsMalformedIsoInputs(string path) =>
        Assert.Throws<InvalidDataException>(() => BinaryInput.Split(path));

    [Fact]
    public void ResolvesImageWithoutTreatingDiscPathAsHostPath()
    {
        var root = Path.GetFullPath("input-root");
        var path = BinaryInput.Resolve(root, "iso:Mario Kart.iso#/rel/StaticR.rel");
        var (image, entry) = BinaryInput.Split(path);
        Assert.Equal(Path.Combine(root, "Mario Kart.iso"), image);
        Assert.Equal("/rel/StaticR.rel", entry);
    }

    [Fact]
    public void PreservesOrdinaryBinaryInput()
    {
        var path = Path.GetTempFileName();
        try {
            File.WriteAllBytes(path, new byte[] {0, 13, 10, 255});
            Assert.True(BinaryInput.Exists(path));
            Assert.Equal(new byte[] {0, 13, 10, 255}, BinaryInput.ReadAllBytes(path));
        } finally { File.Delete(path); }
    }
}
