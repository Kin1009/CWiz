using static cw2tools.CasioInternal.Static;
using static cw2tools.CasioInternal.Bitmap;
using System.Runtime.InteropServices;
using System.IO;
using System.Windows.Media.Imaging;
using System.Text;

var languages = 2;

string BL(nint func)
{
    return $"01 F{(func >> 16) & 0xf:X} {func & 0xff:X2} {(func >> 8) & 0xff:X2}";
}
string B(nint func)
{
    return $"00 F{(func >> 16) & 0xf:X} {func & 0xff:X2} {(func >> 8) & 0xff:X2}";
}

unsafe
{
    rom = (byte*)Marshal.AllocHGlobal(0x40000);
    using (var fs = File.OpenRead("rom.bin"))
        _ = fs.Read(new Span<byte>(rom, 0x40000));
    var mode_num_ref = FindSignature(rom, 0x40000, "ce f8 01 ?? ?? ?? 10 90 ?? ?? 04 70 02 c0 01 10 01 ce") - (nint)rom;
    var mode_num_addr = rom[mode_num_ref + 8] | (rom[mode_num_ref + 9] << 8);
    byte mode_num = rom[mode_num_addr];
    var main_entry = FindSignature(rom, 0x10000, "00 00 11 90 00 f0 00 ?? ?? ??") + 6 - (nint)rom;
    var main_func = (((rom[main_entry + 1]) & 0x0F) << 16) | (rom[main_entry + 2]) | (rom[main_entry + 3] << 8);
    var main_hook = FindSignature(rom + main_func, 0x100, "70 ?? c9 01 ?? ?? ?? ?? ce") + 3 - (nint)rom;
    var comp_func = (((rom[main_hook + 1]) & 0x0F) << 16) | (rom[main_hook + 2]) | (rom[main_hook + 3] << 8);
    var menu_hook = FindSignature(rom, 0x40000, "90 80 00 01 c7 f0 03 c8 04 00 11 90 38 d1 c5 f0 06 e2 01 ?? ?? ??") + 18 - (nint)rom;
    var mul_fn = (((rom[menu_hook + 1]) & 0x0F) << 16) | (rom[menu_hook + 2]) | (rom[menu_hook + 3] << 8);
    var unknown_fn = (((rom[menu_hook + 0x37]) & 0x0F) << 16) | (rom[menu_hook + 0x38]) | (rom[menu_hook + 0x39] << 8);
    var line_print_func = FindSignature(rom, 0x40000, "ce f8 7e f8 05 f8 25 fa 0b 0d 10 90 37 d1 0e 70 01 c9") - (nint)rom;
    var mode_hook_1 = FindSignature(rom, 0x40000, "00 70 01 c8 04 06 00 e4 0d ce 45 f0 06 e2 01 ?? ?? ?? 08 90 ?? ??") + 18 - (nint)rom;
    var mode_hook_2 = FindSignature(rom, 0x40000, "60 80 00 01 06 e2 01 ?? ?? ?? 08 90 ?? ?? 00 85") + 10 - (nint)rom;
    var sys_mode_table = rom[mode_hook_1 + 2] | (rom[mode_hook_1 + 3] << 8);
    var sys_str_table = rom[menu_hook + 0xF6] | (rom[menu_hook + 0xF7] << 8);
    var sys_code_end = FindSignature(rom + 0x20000, 0x20000, "ff ff ff ff ff ff ff ff") + 2 - (nint)rom;
    Console.WriteLine($"[ + ] Patch from {((sys_code_end >> 16) & 0xF):X1}:{(sys_code_end & 0xFFFF):X4} (Y/N)");
    if (Console.ReadLine().ToUpper()[0] == 'N')
    {
        Console.WriteLine("[ * ] Input target addr:");
        sys_code_end = Convert.ToInt32(Console.ReadLine(), 16);
    }
    Console.WriteLine("[ * ] Number of modes to add:");
    byte new_modes = byte.Parse(Console.ReadLine());
    rom[mode_num_addr] += new_modes;
    nint[] pbmp = new nint[new_modes];
    nint[,] pstr = new nint[languages, new_modes];
    nint[] ppstr = new nint[languages];
    nint[] pfunc = new nint[new_modes];
    byte[] mode_codes = new byte[new_modes];
    Console.WriteLine("[ + ] Note: string beginning with '*' is recognized as hex values.");
    for(int i = 0; i < new_modes; i++)
    {
        Console.WriteLine("[ * ] Code for mode " + i + ":");
        mode_codes[i] = Convert.ToByte(Console.ReadLine(), 16);
        Console.WriteLine("[ * ] Icon for mode " + i + ":");
        BitmapImage bitmap = new BitmapImage();
        bitmap.BeginInit();
        bitmap.StreamSource = File.OpenRead(Console.ReadLine());
        bitmap.EndInit();
        Set4(sys_code_end, 48, 23, bitmap);
        pbmp[i] = sys_code_end;
        sys_code_end += 0x8A;
        for(int j = 0; j < languages; j++)
        {
            Console.WriteLine("[ * ] Name for mode " + i + " (language " + j + "):");
            string name_str = Console.ReadLine();
            pstr[j, i] = sys_code_end;
            if (name_str[0] == '*')
            {
                name_str = name_str.ToUpper();
                byte* name = (byte*)Marshal.StringToHGlobalAnsi(name_str).ToPointer();
                name++;
                while (name[0] != 0 && name[1] != 0)
                {
                    rom[sys_code_end++] = GetByte(name);
                    name += 2;
                }
            } else
            {
                ApplyPatch(rom + sys_code_end, Encoding.ASCII.GetBytes(name_str));
                sys_code_end += name_str.Length;
            }
            rom[sys_code_end++] = 0;
            if ((sys_code_end & 1) == 1) sys_code_end++;
        }
        Console.WriteLine("[ * ] Entry addr for mode " + i + ":");
        pfunc[i] = Convert.ToInt32(Console.ReadLine(), 16);
    }
    //Generate look up table
    var icon_table = sys_code_end;
    for(int i = 0; i < new_modes;  i++)
    {
        rom[sys_code_end++] = Convert.ToByte(pbmp[i] & 0xFF);
        rom[sys_code_end++] = Convert.ToByte((pbmp[i] >> 8) & 0xFF);
        rom[sys_code_end++] = Convert.ToByte((pbmp[i] >> 16) & 0xFF);
        rom[sys_code_end++] = 0;
    }
    for(int i = 0; i < languages; i++)
    {
        ppstr[i] = sys_code_end - 6 * (mode_num + 1);
        for(int j = 0; j < new_modes; j++)
        {
            rom[sys_code_end++] = Convert.ToByte(pstr[i, j] & 0xFF);
            rom[sys_code_end++] = Convert.ToByte((pstr[i, j] >> 8) & 0xFF);
            rom[sys_code_end++] = Convert.ToByte((pstr[i, j] >> 16) & 0xFF);
            rom[sys_code_end++] = 0;
            rom[sys_code_end++] = i == 0 ? mode_codes[j] : (byte)0;
            rom[sys_code_end++] = 0;
        }
    }
    var mode_table = ppstr[0] + 4;
    var str_table = sys_code_end;
    for(int i = 0; i < languages; i++)
    {
        rom[sys_code_end++] = Convert.ToByte(ppstr[i] & 0xFF);
        rom[sys_code_end++] = Convert.ToByte((ppstr[i] >> 8) & 0xFF);
    }
    //Generate mode entry func
    ApplyPatch(rom + main_hook, ConvertHexStringToBytes(B(sys_code_end)));
    for(int i = 0; i < new_modes; i++)
    {
        rom[sys_code_end++] = mode_codes[i];
        rom[sys_code_end++] = 0x70;
        rom[sys_code_end++] = Convert.ToByte(4 + i + 2 * (new_modes - 1));
        rom[sys_code_end++] = 0xC9;
    }
    ApplyPatch(rom + sys_code_end, ConvertHexStringToBytes($"{BL(comp_func)} {B(main_hook + 4)}"));
    sys_code_end += 8;
    for(int i = 0;i < new_modes; i++)
    {
        ApplyPatch(rom + sys_code_end, ConvertHexStringToBytes(BL(pfunc[i])));
        sys_code_end += 4;
        rom[sys_code_end++] = (byte)Convert.ToSByte(-3 * (i + 1) - 2);
        rom[sys_code_end++] = 0xCE;
    }

    icon_table -= 4 * mode_num + 4;
    ApplyPatch(rom + menu_hook, ConvertHexStringToBytes(B(sys_code_end)));
    var icon_entry_func = ConvertHexStringToBytes($"""
        {mode_num:X2} 70
        04 c2
        {BL(mul_fn)}
        {B(menu_hook + 4)}
        80 e1
        f6 e1
        1a a2
        6e f4
        00 01
        2b 91
        2a 90
        {((icon_table >> 16) & 0xFF):X2} e3 08 a4 {(icon_table & 0xFF):X2} {((icon_table >> 8) & 0xFF):X2}
        {((icon_table >> 16) & 0xFF):X2} e3 08 96 {((icon_table + 2) & 0xFF):X2} {(((icon_table + 2) >> 8) & 0xFF):X2}
        8a 01
        6f 90 40 90
        21 90
        81 e4
        81 e2
        ff 11
        f9 c8
        2e f4
        1a a0
        {B(menu_hook + 8)}
        """);
    ApplyPatch(rom + sys_code_end, icon_entry_func);
    sys_code_end += icon_entry_func.Length;

    ApplyPatch(rom + menu_hook + 0x36, ConvertHexStringToBytes(B(sys_code_end)));
    var icon_leave_func = ConvertHexStringToBytes($"""
        {BL(unknown_fn)}
        {mode_num:X2} 7c
        02 c2
        {B(menu_hook + 0x3A)}
        40 e1
        4a e1
        fb ce
        """);
    ApplyPatch(rom + sys_code_end, icon_leave_func);
    sys_code_end += icon_leave_func.Length;

    ApplyPatch(rom + menu_hook + 0xF4, ConvertHexStringToBytes(B(sys_code_end)));
    byte DSR = (byte)((str_table >> 16) & 0xFF);
    var str_extract_func = ConvertHexStringToBytes($"""
        {(6 * mode_num):X2} 70
        04 c2
        08 a2 {(sys_str_table & 0xFF):X2} {(((sys_str_table + 2) >> 8) & 0xFF):X2}
        {B(menu_hook + 0xF8)}
        f2 e1
        1a a2
        2a f0
        10 92 2b d1
        00 03
        26 f2
        {DSR:X2} e3 28 a2 {(str_table & 0xFF):X2} {((str_table >> 8) & 0xFF):X2}
        26 f0
        {DSR:X2} e3 02 92
        82 e0
        {DSR:X2} e3 00 91
        02 ce
        81 e2
        51 90
        1f 90 20 90
        fb c8
        31 90
        33 01
        16 00
        1a a2
        {BL(line_print_func)}
        0e e1
        {B(menu_hook + 0x100)}
        """);
    ApplyPatch(rom + sys_code_end, str_extract_func);
    sys_code_end += str_extract_func.Length;

    ApplyPatch(rom + mode_hook_1, ConvertHexStringToBytes(BL(sys_code_end)));
    ApplyPatch(rom + mode_hook_2, ConvertHexStringToBytes(BL(sys_code_end)));
    ApplyPatch(rom + mode_hook_2 + 0x1E, ConvertHexStringToBytes(BL(sys_code_end)));
    var mode_sel_func = ConvertHexStringToBytes($"""
        {(6 * mode_num):X2} 70
        03 c2
        08 90 {(sys_mode_table & 0xFF):X2} {(((sys_mode_table + 2) >> 8) & 0xFF):X2}
        1f fe
        {((mode_table >> 16) & 0xFF):X2} e3 08 90 {(mode_table & 0xFF):X2} {((mode_table >> 8) & 0xFF):X2}
        1f fe
        """);
    ApplyPatch(rom + sys_code_end, mode_sel_func);

    using (var fs = File.OpenWrite("rom_patched.bin"))
        fs.Write(new Span<byte>(rom, 0x40000));

    Console.WriteLine("[ + ] Done");
}
