// Minimal stand-ins for the WPF types used by the reference C# patchers.
// This lets the *unmodified* Program.cs run on macOS (mono/dotnet core), so it
// can produce golden ROMs for the C++ port.
using System;
using System.IO;
using System.IO.Compression;
using System.Text;

namespace System.Windows {
    public struct Int32Rect {
        public int X, Y, Width, Height;
        public Int32Rect(int x, int y, int w, int h) { X = x; Y = y; Width = w; Height = h; }
        public static Int32Rect Empty => new Int32Rect(0, 0, 0, 0);
    }
}

namespace System.Windows.Media.Imaging {
    public sealed class BitmapImage {
        public Stream StreamSource { get; set; } = null!;
        private byte[] rgba = Array.Empty<byte>();
        private int w, h;
        public int PixelWidth => w;
        public int PixelHeight => h;
        public void BeginInit() { }
        public void EndInit() {
            using var ms = new MemoryStream();
            StreamSource.CopyTo(ms);
            rgba = Png.Decode(ms.ToArray(), out w, out h);
        }
        // WPF's IntPtr overload is (rect, buffer, bufferSize, stride).
        public void CopyPixels(Int32Rect rect, IntPtr buffer, int bufferSize, int stride) {
            int rw = Math.Min(rect.Width, w), rh = Math.Min(rect.Height, h);
            int need = rh > 0 ? stride * (rh - 1) + rw * 4 : 0;
            if (bufferSize < need) throw new ArgumentException("buffer too small");
            unsafe {
                byte* dst = (byte*)buffer;
                for (int y = 0; y < rh; y++) {
                    for (int x = 0; x < rw; x++) {
                        int si = ((rect.Y + y) * w + (rect.X + x)) * 4;
                        byte* d = dst + y * stride + x * 4;
                        d[0] = rgba[si + 2]; // B
                        d[1] = rgba[si + 1]; // G
                        d[2] = rgba[si + 0]; // R
                        d[3] = 0;
                    }
                }
            }
        }
    }

    internal static class Png {
        private static uint BE32(byte[] b, int o) =>
            (uint)((b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]);

        public static byte[] Decode(byte[] f, out int width, out int height) {
            if (f.Length < 8 || f[0] != 0x89 || f[1] != 'P' || f[2] != 'N' || f[3] != 'G')
                throw new InvalidDataException("not a PNG");
            int pos = 8, w = 0, h = 0, bit = 0, color = 0, interlace = 0;
            var idat = new MemoryStream();
            while (pos + 8 <= f.Length) {
                int len = (int)BE32(f, pos);
                string type = Encoding.ASCII.GetString(f, pos + 4, 4);
                int data = pos + 8;
                if (type == "IHDR") {
                    w = (int)BE32(f, data);
                    h = (int)BE32(f, data + 4);
                    bit = f[data + 8];
                    color = f[data + 9];
                    interlace = f[data + 12];
                } else if (type == "IDAT") {
                    idat.Write(f, data, len);
                } else if (type == "IEND") break;
                pos = data + len + 4;
            }
            if (bit != 8 || interlace != 0) throw new InvalidDataException("need 8-bit non-interlaced PNG");
            int channels = color switch { 0 => 1, 2 => 3, 4 => 2, 6 => 4, _ => throw new InvalidDataException("palette PNG unsupported") };
            var comp = new MemoryStream(idat.ToArray());
            using var z = new ZLibStream(comp, CompressionMode.Decompress);
            using var rawms = new MemoryStream();
            z.CopyTo(rawms);
            byte[] raw = rawms.ToArray();
            int rowBytes = w * channels;
            if (raw.Length < (long)h * (rowBytes + 1)) throw new InvalidDataException("truncated PNG");
            byte[] cur = new byte[rowBytes], prev = new byte[rowBytes];
            byte[] outRgba = new byte[w * h * 4];
            int o = 0;
            for (int y = 0; y < h; y++) {
                int filter = raw[o++];
                for (int x = 0; x < rowBytes; x++) {
                    int a = x >= channels ? cur[x - channels] : 0;
                    int b = prev[x];
                    int c = x >= channels ? prev[x - channels] : 0;
                    int v = raw[o++];
                    int r = filter switch {
                        0 => v,
                        1 => v + a,
                        2 => v + b,
                        3 => v + ((a + b) >> 1),
                        4 => v + Paeth(a, b, c),
                        _ => throw new InvalidDataException("bad PNG filter")
                    };
                    cur[x] = (byte)r;
                }
                for (int x = 0; x < w; x++) {
                    int si = x * channels;
                    int di = (y * w + x) * 4;
                    switch (channels) {
                        case 1:
                            outRgba[di] = outRgba[di + 1] = outRgba[di + 2] = cur[si];
                            outRgba[di + 3] = 255;
                            break;
                        case 2:
                            outRgba[di] = outRgba[di + 1] = outRgba[di + 2] = cur[si];
                            outRgba[di + 3] = cur[si + 1];
                            break;
                        case 3:
                            outRgba[di] = cur[si]; outRgba[di + 1] = cur[si + 1]; outRgba[di + 2] = cur[si + 2];
                            outRgba[di + 3] = 255;
                            break;
                        default:
                            outRgba[di] = cur[si]; outRgba[di + 1] = cur[si + 1]; outRgba[di + 2] = cur[si + 2];
                            outRgba[di + 3] = cur[si + 3];
                            break;
                    }
                }
                (prev, cur) = (cur, prev);
            }
            width = w; height = h;
            return outRgba;
        }

        private static int Paeth(int a, int b, int c) {
            int p = a + b - c, pa = Math.Abs(p - a), pb = Math.Abs(p - b), pc = Math.Abs(p - c);
            return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
        }
    }
}

namespace cw2tools.CasioInternal {
    // Verbatim ports of Set3/Set4 from the reference Bitmap.cs, using the shim
    // above instead of a WriteableBitmap.
    internal static unsafe class Bitmap {
        public static void Set3(nint rb1, nint rb2, int width, int height, System.Windows.Media.Imaging.BitmapImage bi) {
            int w2 = width / 8;
            byte* data = stackalloc byte[width * height * 4];
            bi.CopyPixels(new System.Windows.Int32Rect(0, 0, width, height), (nint)data, width * height * 4, width * 4);

            byte* array1 = Static.rom + rb1;
            byte* array2 = Static.rom + rb2;

            for (int j = 0; j < height; j++) {
                for (int i = 0; i < w2; i++) {
                    for (int k = 0; k < 8; k++) {
                        var b = data[j * width * 4 + i * 8 * 4 + k * 4 + 0] >> 6;
                        if ((b & 0b10) == 0)
                            array2[j * w2 + i] |= (byte)(0x80 >> k);
                        else
                            array2[j * w2 + i] &= (byte)(~(0x80 >> k));
                        if ((b & 0b1) == 0)
                            array1[j * w2 + i] |= (byte)(0x80 >> k);
                        else
                            array1[j * w2 + i] &= (byte)(~(0x80 >> k));
                    }
                }
            }
        }

        public static void Set4(nint rb, int width, int height, System.Windows.Media.Imaging.BitmapImage bi) {
            int w2 = width / 8;
            byte* data = stackalloc byte[width * height * 4];
            bi.CopyPixels(new System.Windows.Int32Rect(0, 0, width, height), (nint)data, width * height * 4, width * 4);

            byte* array = Static.rom + rb;

            for (int j = 0; j < height; j++) {
                for (int i = 0; i < w2; i++) {
                    for (int k = 0; k < 8; k++) {
                        var b = data[j * width * 4 + i * 8 * 4 + k * 4 + 0] >> 7;
                        if ((b & 0b1) == 0)
                            array[j * w2 + i] |= (byte)(0x80 >> k);
                        else
                            array[j * w2 + i] &= (byte)(~(0x80 >> k));
                    }
                }
            }
        }
    }
}
