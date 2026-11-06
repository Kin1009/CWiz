using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace cw2tools.CasioInternal
{
    public static unsafe class Static
    {
        public static byte* rom;
        public static bool is_cwii;
        public static ushort be_read(byte* ptr)
        {
            return (ushort)((ptr[0] << 8) | ptr[1]);
        }
        public static IntPtr FindSignature(byte* start, nint size, string signature)
        {
            signature = signature.ToUpper();
            byte* pattern = (byte*)Marshal.StringToHGlobalAnsi(signature).ToPointer();
            byte* patBegin = pattern;
            byte* oldPat = pattern;
            byte* end = start + size;
            IntPtr firstMatch = IntPtr.Zero;

            byte high = (byte)((pattern[0] >= '0' && pattern[0] <= '9' ? pattern[0] - '0' : pattern[0] - 'A' + 10) << 4);
            byte low = (byte)(pattern[1] >= '0' && pattern[1] <= '9' ? pattern[1] - '0' : pattern[1] - 'A' + 10);

            for (byte* pCur = start; pCur < end; pCur++)
            {
                while (*pattern == ' ')
                    pattern++;

                if (*pattern == 0)
                    return firstMatch;

                if (oldPat != pattern)
                {
                    oldPat = pattern;
                    high = (byte)((pattern[0] >= '0' && pattern[0] <= '9' ? pattern[0] - '0' : pattern[0] - 'A' + 10) << 4);
                    low = (byte)(pattern[1] >= '0' && pattern[1] <= '9' ? pattern[1] - '0' : pattern[1] - 'A' + 10);
                }

                if ((pattern[0] == '?' || (*pCur & 0xF0) == high) && (pattern[1] == '?' || (*pCur & 0x0F) == low))
                {
                    if (firstMatch == IntPtr.Zero)
                        firstMatch = (IntPtr)pCur;

                    if (pattern[1] == 0 || pattern[2] == 0)
                        return firstMatch;

                    pattern += 2;
                }
                else
                {
                    pattern = patBegin;
                    if (firstMatch != IntPtr.Zero) pCur = (byte*)firstMatch;
                    firstMatch = IntPtr.Zero;
                }
            }

            return IntPtr.Zero;
        }
        public static byte* FindStringInMemory(byte* memory, nint length, string str)
        {
            if (memory == null || length <= 0 || string.IsNullOrEmpty(str))
            {
                throw new ArgumentException("Invalid arguments provided.");
            }

            byte[] searchBytes = Encoding.UTF8.GetBytes(str);
            fixed (byte* searchPtr = searchBytes)
            {
                for (nint i = 0; i <= length - searchBytes.Length; i++)
                {
                    if (CompareBytes(memory + i, searchPtr, searchBytes.Length))
                    {
                        return memory + i;
                    }
                }
            }

            return null;
        }

        private static bool CompareBytes(byte* memory, byte* search, int length)
        {
            for (int i = 0; i < length; i++)
            {
                if (memory[i] != search[i])
                {
                    return false;
                }
            }

            return memory[length] == 0;
        }
        public static byte[] ConvertHexStringToBytes(string hexString)
        {
            hexString = hexString.Replace(Environment.NewLine, "").Replace(" ", "").Replace("\t", "").Replace("\r", "");
            if ((hexString.Length & 1) != 0) { Console.Error.WriteLine("ODD HEX: [" + hexString + "]"); }
            int length = hexString.Length;
            byte[] bytes = new byte[length / 2];
            for (int i = 0; i < length; i += 2)
            {
                bytes[i / 2] = Convert.ToByte(hexString.Substring(i, 2), 16);
            }
            Console.WriteLine("[ + ] Build payload:");
            int j = 0;
            while (true)
            {
                for (int i = 0; i < 16; i++)
                {
                    if (j >= bytes.Length)
                    {
                        Console.WriteLine();
                        return bytes;
                    }
                    Console.Write($"{bytes[j++]:X2} ");
                }
                Console.WriteLine();
            }
        }

        public static void ApplyPatch(byte* rom, byte[] patch)
        {
            for (int i = 0; i < patch.Length; i++)
            {
                rom[i] = patch[i];
            }
        }
    }
}
