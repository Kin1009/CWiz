using static cw2tools.CasioInternal.Static;
using static cw2tools.CasioInternal.Bitmap;
using System.Runtime.InteropServices;
using System.IO;
using System.Windows.Media.Imaging;

var languages = 2;

string BL(nint func)
{
    return $"01 F{(func >> 16) & 0xf:X} {func & 0xff:X2} {(func >> 8) & 0xff:X2}";
}
string B(nint func)
{
    return $"00 F{(func >> 16) & 0xf:X} {func & 0xff:X2} {(func >> 8) & 0xff:X2}";
}

nint A(nint addr)
{
    return addr >= 0x5E000 && addr < 0x60000 ? addr + 0x12000 : addr;
}

unsafe
{
    rom = (byte*)Marshal.AllocHGlobal(0x60000);
    using (var fs = File.OpenRead("rom.bin"))
        _ = fs.Read(new Span<byte>(rom, 0x60000));
    var mode_num_ref = FindSignature(rom, 0x60000, "ce f8 01 ?? ?? ?? 10 90 ?? ?? 03 70 02 c0 01 10 01 ce") - (nint)rom;
    var mode_num_addr = rom[mode_num_ref + 8] | (rom[mode_num_ref + 9] << 8);
    byte mode_num = rom[mode_num_addr];
    var main_entry = FindSignature(rom, 0x10000, "00 00 11 90 00 f0 00 ?? ?? ??") + 6 - (nint)rom;
    var main_func = (((rom[main_entry + 1]) & 0x0F) << 16) | (rom[main_entry + 2]) | (rom[main_entry + 3] << 8);
    var main_hook = FindSignature(rom + main_func, 0x200, "70 ?? c9 01 f? ?? ?? 01 f? ?? ?? ?? ce") + 3 - (nint)rom;
    var comp_func = (((rom[main_hook + 1]) & 0x0F) << 16) | (rom[main_hook + 2]) | (rom[main_hook + 3] << 8);
    var menu_hook = FindSignature(rom, 0x60000, "02 e1 a0 80 00 01 3b 91 3a 90 08 a2 ?? ?? 08 94") + 10 - (nint)rom;
    var mode_hook_1 = FindSignature(rom, 0x60000, "00 84 00 70 01 c8 03 04 00 e2 0c ce 25 f0 3b 91 3a 90 08 90") + 18 - (nint)rom;
    var mode_hook_2 = FindSignature(rom, 0x60000, "60 80 00 01 3b 91 3a 90 08 90 ?? ?? 00 84 00 f?") + 8 - (nint)rom;
    var sys_mode_table = (rom[mode_hook_1 + 2] | (rom[mode_hook_1 + 3] << 8)) - 7;
    var sys_str_table = rom[menu_hook + 0x8E] | (rom[menu_hook + 0x8F] << 8);
    var sys_code_end = FindSignature(rom + 0x10000, 0x50000, "ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff") + 2 - (nint)rom;
    if ((sys_code_end & 1) != 0) sys_code_end++;
    Console.WriteLine($"[ + ] Patch from {((A(sys_code_end) >> 16) & 0xF):X1}:{(A(sys_code_end) & 0xFFFF):X4} (Y/N)");
    if (Console.ReadLine().ToUpper()[0] == 'N')
    {
        Console.WriteLine("[ * ] Input target addr:");
        sys_code_end = Convert.ToInt32(Console.ReadLine(), 16);
        if (sys_code_end >= 0x70000) sys_code_end -= 0x12000;
    }
    Console.WriteLine("[ * ] Number of modes to add:");
    byte new_modes = byte.Parse(Console.ReadLine());
    rom[mode_num_addr] += new_modes;
    nint[] pbmp = new nint[new_modes];
    nint[,] pstr = new nint[languages, new_modes];
    nint[] ppstr = new nint[languages];
    nint[] pfunc = new nint[new_modes];
    byte[] mode_codes = new byte[new_modes];
    for (int i = 0; i < new_modes; i++)
    {
        Console.WriteLine("[ * ] Code for mode " + i + ":");
        mode_codes[i] = Convert.ToByte(Console.ReadLine(), 16);
        Console.WriteLine("[ * ] Icon for mode " + i + ":");
        BitmapImage bitmap = new BitmapImage();
        bitmap.BeginInit();
        bitmap.StreamSource = File.OpenRead(Console.ReadLine());
        bitmap.EndInit();
        Set3(sys_code_end, sys_code_end + 0x80, 64, 16, bitmap);
        pbmp[i] = sys_code_end;
        sys_code_end += 0x100;
        for (int j = 0; j < languages; j++)
        {
            Console.WriteLine("[ * ] Text image for mode " + i + " (language " + j + "):");
            BitmapImage tbmp = new BitmapImage();
            tbmp.BeginInit();
            tbmp.StreamSource = File.OpenRead(Console.ReadLine());
            tbmp.EndInit();
            Set4(sys_code_end, 64, 13, tbmp);
            pstr[j, i] = sys_code_end;
            sys_code_end += 0x68;
        }
        Console.WriteLine("[ * ] Entry addr for mode " + i + ":");
        pfunc[i] = Convert.ToInt32(Console.ReadLine(), 16);
    }
    //Generate look up table
    var icon_table = sys_code_end - 8 * mode_num - 8;
    for (int i = 0; i < new_modes; i++)
    {
        rom[sys_code_end++] = Convert.ToByte(A(pbmp[i]) & 0xFF);
        rom[sys_code_end++] = Convert.ToByte((A(pbmp[i]) >> 8) & 0xFF);
        rom[sys_code_end++] = Convert.ToByte((A(pbmp[i]) >> 16) & 0xFF);
        rom[sys_code_end++] = 0;
        rom[sys_code_end++] = Convert.ToByte((A(pbmp[i]) + 0x80) & 0xFF);
        rom[sys_code_end++] = Convert.ToByte(((A(pbmp[i]) + 0x80) >> 8) & 0xFF);
        rom[sys_code_end++] = Convert.ToByte(((A(pbmp[i]) + 0x80) >> 16) & 0xFF);
        rom[sys_code_end++] = mode_codes[i];
    }
    for (int i = 0; i < languages; i++)
    {
        ppstr[i] = sys_code_end - 4 * ((languages > 2 ? mode_num : 14) + 1);
        for (int j = 0; j < new_modes; j++)
        {
            if (j + mode_num < 14 && languages <= 2)
            {
                var ptr = sys_str_table + (mode_num + j + 1) * 4 + i * 60;
                rom[ptr] = Convert.ToByte(A(pstr[i, j]) & 0xFF);
                rom[ptr + 1] = Convert.ToByte((A(pstr[i, j]) >> 8) & 0xFF);
                rom[ptr + 2] = Convert.ToByte((A(pstr[i, j]) >> 16) & 0xFF);
            }
            else
            {
                rom[sys_code_end++] = Convert.ToByte(A(pstr[i, j]) & 0xFF);
                rom[sys_code_end++] = Convert.ToByte((A(pstr[i, j]) >> 8) & 0xFF);
                rom[sys_code_end++] = Convert.ToByte((A(pstr[i, j]) >> 16) & 0xFF);
                rom[sys_code_end++] = 0;
            }
        }
    }
    var str_table = sys_code_end;
    if(languages > 2 || mode_num + new_modes > 14)
    {
        for (int i = 0; i < languages; i++)
        {
            rom[sys_code_end++] = Convert.ToByte(A(ppstr[i]) & 0xFF);
            rom[sys_code_end++] = Convert.ToByte((A(ppstr[i]) >> 8) & 0xFF);
            rom[sys_code_end++] = Convert.ToByte((A(ppstr[i]) >> 16) & 0xFF);
            rom[sys_code_end++] = 0;
        }
    }
    //Generate mode entry func
    ApplyPatch(rom + main_hook, ConvertHexStringToBytes(B(A(sys_code_end))));
    for (int i = 0; i < new_modes; i++)
    {
        rom[sys_code_end++] = mode_codes[i];
        rom[sys_code_end++] = 0x70;
        rom[sys_code_end++] = Convert.ToByte(4 + i + 2 * (new_modes - 1));
        rom[sys_code_end++] = 0xC9;
    }
    ApplyPatch(rom + sys_code_end, ConvertHexStringToBytes($"{BL(comp_func)} {B(main_hook + 4)}"));
    sys_code_end += 8;
    for (int i = 0; i < new_modes; i++)
    {
        ApplyPatch(rom + sys_code_end, ConvertHexStringToBytes(BL(pfunc[i])));
        sys_code_end += 4;
        rom[sys_code_end++] = (byte)Convert.ToSByte(-3 * (i + 1) - 2);
        rom[sys_code_end++] = 0xCE;
    }
    ApplyPatch(rom + menu_hook, ConvertHexStringToBytes(B(A(sys_code_end))));
    var icon_func = ConvertHexStringToBytes($"""
        {(mode_num << 3):X2} 70
        04 c2
        08 a2 {((sys_mode_table + 4) & 0xFF):X2} {(((sys_mode_table + 4) >> 8) & 0xFF):X2}
        {B(menu_hook + 4)}
        {(((A(icon_table) + 4) >> 16) & 0xFF):X2} e3 08 a2 {((A(icon_table) + 4) & 0xFF):X2} {(((A(icon_table) + 4) >> 8) & 0xFF):X2}
        {(((A(icon_table) + 6) >> 16) & 0xFF):X2} e3 08 94 {((A(icon_table) + 6) & 0xFF):X2} {(((A(icon_table) + 6) >> 8) & 0xFF):X2}
        f8 b2
        {((A(icon_table) >> 16) & 0xFF):X2} e3 08 a2 {(A(icon_table) & 0xFF):X2} {((A(icon_table) >> 8) & 0xFF):X2}
        {(((A(icon_table) + 2) >> 16) & 0xFF):X2} e3 08 90 {((A(icon_table) + 2) & 0xFF):X2} {(((A(icon_table) + 2) >> 8) & 0xFF):X2}
        {B(menu_hook + 0x12)}
        """);
    ApplyPatch(rom + sys_code_end, icon_func);
    sys_code_end += icon_func.Length;

    if(languages > 2 || new_modes + mode_num > 14)
    {
        ApplyPatch(rom + menu_hook + 0x92, ConvertHexStringToBytes(BL(A(sys_code_end))));
        ApplyPatch(rom + menu_hook + 0xB0, ConvertHexStringToBytes(BL(A(sys_code_end))));
        ApplyPatch(rom + menu_hook + 0xEE, ConvertHexStringToBytes(BL(A(sys_code_end))));
        ApplyPatch(rom + menu_hook + 0x10E, ConvertHexStringToBytes(BL(A(sys_code_end))));
        var text_func = ConvertHexStringToBytes($"""
        {(languages > 2 ? mode_num + 1 : 0x0F):X2} 7a
        03 c0
        c8 92 {((sys_str_table + 2) & 0xFF):X2} {(((sys_str_table + 2) >> 8) & 0xFF):X2}
        1f fe
        10 92 bb 91
        00 03
        2b 93
        2a 92
        {((A(str_table) >> 16) & 0xFF):X2} e3 28 a0 {(A(str_table) & 0xFF):X2} {((A(str_table) >> 8) & 0xFF):X2}
        {(((A(str_table) + 2) >> 16) & 0xFF):X2} e3 28 92 {((A(str_table) + 2) & 0xFF):X2} {(((A(str_table) + 2) >> 8) & 0xFF):X2}
        a0 8c
        00 0d
        2b 9d
        2a 9c
        06 fc
        2f 90 c2 90
        66 f0
        2f 90 02 d2
        1f fe
        """);

        ApplyPatch(rom + sys_code_end, text_func);
        sys_code_end += text_func.Length;
    }

    ApplyPatch(rom + mode_hook_1, ConvertHexStringToBytes(BL(A(sys_code_end))));
    ApplyPatch(rom + mode_hook_2, ConvertHexStringToBytes(BL(A(sys_code_end))));
    ApplyPatch(rom + mode_hook_2 + 0x12, ConvertHexStringToBytes(BL(A(sys_code_end))));
    var mode_sel_func = ConvertHexStringToBytes($"""
        {(8 * mode_num):X2} 70
        03 c2
        08 90 {((sys_mode_table + 7) & 0xFF):X2} {(((sys_mode_table + 7) >> 8) & 0xFF):X2}
        1f fe
        {(((A(icon_table) + 7) >> 16) & 0xFF):X2} e3 08 90 {((A(icon_table) + 7) & 0xFF):X2} {(((A(icon_table) + 7) >> 8) & 0xFF):X2}
        1f fe
        """);
    ApplyPatch(rom + sys_code_end, mode_sel_func);

    using (var fs = File.OpenWrite("rom_patched.bin"))
        fs.Write(new Span<byte>(rom, 0x60000));

    Console.WriteLine("[ + ] Done");
}
