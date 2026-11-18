using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;

namespace Kurisu.Debugging
{
    public sealed class PdbIdentity
    {
        public string ModuleName { get; set; }
        public string PdbName { get; set; }
        public Guid Guid { get; set; }
        public uint Age { get; set; }

        public string StoreKey
        {
            get
            {
                return Guid.ToString("N").ToUpperInvariant() +
                    Age.ToString("x", CultureInfo.InvariantCulture);
            }
        }
    }

    // Read-only parsers for MINIDUMP_MODULE/RSDS and the MSF 7 PDB info stream.
    // These formats are documented by Microsoft and LLVM respectively.
    public static class CrashDumpSymbols
    {
        private static byte[] ReadAt(BinaryReader reader, long offset, int count)
        {
            if (offset < 0 || count < 0 || offset > reader.BaseStream.Length - count)
                throw new InvalidDataException("File is truncated or contains an invalid offset.");
            reader.BaseStream.Position = offset;
            byte[] bytes = reader.ReadBytes(count);
            if (bytes.Length != count)
                throw new EndOfStreamException("Could not read the requested file data.");
            return bytes;
        }

        private static uint ReadUInt32(BinaryReader reader, long offset)
        {
            return BitConverter.ToUInt32(ReadAt(reader, offset, 4), 0);
        }

        private static string ReadModuleName(BinaryReader reader, uint offset)
        {
            uint length = ReadUInt32(reader, offset);
            if (length > 65536 || (length & 1) != 0)
                throw new InvalidDataException("Invalid minidump module name.");
            return Path.GetFileName(Encoding.Unicode.GetString(
                ReadAt(reader, (long)offset + 4, (int)length)));
        }

        private static string ReadPdbName(byte[] codeView)
        {
            int end = Array.IndexOf(codeView, (byte)0, 24);
            if (end < 0)
                throw new InvalidDataException("The RSDS PDB name is not terminated.");
            string name = Path.GetFileName(Encoding.UTF8.GetString(codeView, 24, end - 24));
            if (String.IsNullOrWhiteSpace(name) ||
                name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 ||
                !name.EndsWith(".pdb", StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Invalid RSDS PDB file name.");
            return name;
        }

        public static PdbIdentity[] ReadDump(string dumpPath)
        {
            var identities = new List<PdbIdentity>();
            using (var reader = new BinaryReader(File.OpenRead(dumpPath)))
            {
                byte[] header = ReadAt(reader, 0, 32);
                if (BitConverter.ToUInt32(header, 0) != 0x504d444d)
                    throw new InvalidDataException("The file is not a Windows minidump.");
                uint streamCount = BitConverter.ToUInt32(header, 8);
                uint directoryOffset = BitConverter.ToUInt32(header, 12);
                if ((long)directoryOffset + (long)streamCount * 12 > reader.BaseStream.Length)
                    throw new InvalidDataException("Invalid minidump stream directory.");

                for (uint streamIndex = 0; streamIndex < streamCount; ++streamIndex)
                {
                    long streamEntry = (long)directoryOffset + streamIndex * 12L;
                    if (ReadUInt32(reader, streamEntry) != 4) // ModuleListStream
                        continue;
                    uint streamSize = ReadUInt32(reader, streamEntry + 4);
                    uint moduleOffset = ReadUInt32(reader, streamEntry + 8);
                    uint moduleCount = ReadUInt32(reader, moduleOffset);
                    if (4L + moduleCount * 108L > streamSize ||
                        (long)moduleOffset + streamSize > reader.BaseStream.Length)
                        throw new InvalidDataException("Invalid minidump module list.");

                    for (uint moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
                    {
                        long moduleEntry = (long)moduleOffset + 4 + moduleIndex * 108L;
                        string moduleName = ReadModuleName(reader,
                            ReadUInt32(reader, moduleEntry + 20));
                        if (!String.Equals(moduleName, "KurisuEditor.exe", StringComparison.OrdinalIgnoreCase) &&
                            !String.Equals(moduleName, "KurisuGame.exe", StringComparison.OrdinalIgnoreCase))
                            continue;

                        uint codeViewSize = ReadUInt32(reader, moduleEntry + 76);
                        uint codeViewOffset = ReadUInt32(reader, moduleEntry + 80);
                        if (codeViewSize < 25 || codeViewSize > 65536)
                            throw new InvalidDataException("The Kurisu module has no supported RSDS record.");
                        byte[] codeView = ReadAt(reader, codeViewOffset, (int)codeViewSize);
                        if (BitConverter.ToUInt32(codeView, 0) != 0x53445352)
                            throw new InvalidDataException("The Kurisu module does not use RSDS symbols.");
                        byte[] guidBytes = new byte[16];
                        Array.Copy(codeView, 4, guidBytes, 0, 16);
                        identities.Add(new PdbIdentity {
                            ModuleName = moduleName,
                            PdbName = ReadPdbName(codeView),
                            Guid = new Guid(guidBytes),
                            Age = BitConverter.ToUInt32(codeView, 20)
                        });
                    }
                }
            }
            if (identities.Count == 0)
                throw new InvalidDataException("No KurisuEditor.exe or KurisuGame.exe RSDS record was found.");
            return identities.ToArray();
        }

        private static long BlockOffset(uint block, uint blockSize, uint blockCount)
        {
            if (block >= blockCount)
                throw new InvalidDataException("Invalid PDB block index.");
            return (long)block * blockSize;
        }

        private static PdbIdentity ReadPdb(BinaryReader reader)
        {
            byte[] header = ReadAt(reader, 0, 56);
            byte[] magic = Encoding.ASCII.GetBytes("Microsoft C/C++ MSF 7.00\r\n\x1a" + "DS\0\0\0");
            for (int index = 0; index < magic.Length; ++index)
                if (header[index] != magic[index])
                    throw new InvalidDataException("The file is not a native MSF 7 PDB.");

            uint blockSize = BitConverter.ToUInt32(header, 32);
            uint blockCount = BitConverter.ToUInt32(header, 40);
            uint directorySize = BitConverter.ToUInt32(header, 44);
            uint blockMap = BitConverter.ToUInt32(header, 52);
            if (blockSize < 512 || blockSize > 65536 || (blockSize & (blockSize - 1)) != 0 ||
                (long)blockSize * blockCount > reader.BaseStream.Length ||
                directorySize < 12 || directorySize > 64 * 1024 * 1024)
                throw new InvalidDataException("Invalid PDB superblock.");
            uint directoryBlocks = (uint)(((long)directorySize + blockSize - 1) / blockSize);
            if (directoryBlocks > blockSize / 4)
                throw new InvalidDataException("The PDB directory block map is too large for this reader.");
            byte[] blockNumbers = ReadAt(reader,
                BlockOffset(blockMap, blockSize, blockCount), checked((int)directoryBlocks * 4));
            byte[] directory = new byte[directorySize];
            for (uint blockIndex = 0; blockIndex < directoryBlocks; ++blockIndex)
            {
                int destination = checked((int)(blockIndex * blockSize));
                int length = Math.Min((int)blockSize, directory.Length - destination);
                uint block = BitConverter.ToUInt32(blockNumbers, (int)blockIndex * 4);
                Array.Copy(ReadAt(reader, BlockOffset(block, blockSize, blockCount), length),
                    0, directory, destination, length);
            }

            uint streamCount = BitConverter.ToUInt32(directory, 0);
            if (streamCount < 2 || 4L + streamCount * 4L > directory.Length)
                throw new InvalidDataException("The PDB info stream is missing.");
            uint firstStreamSize = BitConverter.ToUInt32(directory, 4);
            uint infoSize = BitConverter.ToUInt32(directory, 8);
            long firstStreamBlocks = firstStreamSize == UInt32.MaxValue ? 0 :
                ((long)firstStreamSize + blockSize - 1) / blockSize;
            long infoBlockOffset = 4L + streamCount * 4L + firstStreamBlocks * 4;
            if (infoSize == UInt32.MaxValue || infoSize < 28 || infoBlockOffset + 4 > directory.Length)
                throw new InvalidDataException("Invalid PDB info stream.");
            uint infoBlock = BitConverter.ToUInt32(directory, (int)infoBlockOffset);
            byte[] info = ReadAt(reader, BlockOffset(infoBlock, blockSize, blockCount), 28);
            byte[] guidBytes = new byte[16];
            Array.Copy(info, 12, guidBytes, 0, 16);
            return new PdbIdentity { Guid = new Guid(guidBytes), Age = BitConverter.ToUInt32(info, 8) };
        }

        private static void CheckPdb(BinaryReader reader, PdbIdentity expected)
        {
            PdbIdentity actual = ReadPdb(reader);
            if (actual.Guid != expected.Guid || actual.Age != expected.Age)
                throw new InvalidDataException(String.Format(CultureInfo.InvariantCulture,
                    "PDB mismatch: expected GUID {0}, Age {1}; found GUID {2}, Age {3}.",
                    expected.Guid, expected.Age, actual.Guid, actual.Age));
        }

        public static void VerifyAndCache(string sharedPdb, string cachedPdb, PdbIdentity expected)
        {
            // Verify the share even if a matching PDB is already cached locally.
            using (var source = new BinaryReader(new FileStream(
                sharedPdb, FileMode.Open, FileAccess.Read, FileShare.Read)))
            {
                CheckPdb(source, expected);
                try
                {
                    PrepareCache(source, cachedPdb, expected);
                }
                catch (Exception error)
                {
                    throw new IOException("Shared PDB identity matches, but cache preparation failed for '" +
                        cachedPdb + "': " + error.Message, error);
                }
            }
        }

        private static void PrepareCache(BinaryReader source, string cachedPdb, PdbIdentity expected)
        {
            if (File.Exists(cachedPdb))
            {
                try
                {
                    using (var cached = new BinaryReader(File.OpenRead(cachedPdb)))
                        CheckPdb(cached, expected);
                    return;
                }
                catch (InvalidDataException) { } // Replace an invalid cache entry below.
            }

            Directory.CreateDirectory(Path.GetDirectoryName(cachedPdb));
            string temporaryPath = cachedPdb + "." + Guid.NewGuid().ToString("N") + ".tmp";
            try
            {
                source.BaseStream.Position = 0;
                using (var destination = new FileStream(
                    temporaryPath, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                    source.BaseStream.CopyTo(destination);
                if (File.Exists(cachedPdb))
                    File.Replace(temporaryPath, cachedPdb, null);
                else
                    File.Move(temporaryPath, cachedPdb);
            }
            finally
            {
                if (File.Exists(temporaryPath))
                    File.Delete(temporaryPath);
            }
        }
    }
}
