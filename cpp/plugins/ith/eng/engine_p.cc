// eng/engine_p.cc
// 8/9/2013 jichi
// Branch: ITH_Engine/engine.cpp, revision 133
//
// 8/24/2013 TODO: Clean up the code

#ifdef _MSC_VER
# pragma warning (disable:4100)   // C4100: unreference formal parameter
#endif // _MSC_VER

#include "engine_p.h"
#include "util.h"
#include "ith/cli/cli.h"
#include "ith/sys/sys.h"
#include "ith/common/except.h"
#include "disasm/disasm.h"
#include "cc/ccmacro.h"

//#define ConsoleOutput(...)  (void)0     // jichi 8/18/2013: I don't need ConsoleOutput

//#define DEBUG "engine_p.h"

#ifdef DEBUG
# include "ith/common/growl.h"
namespace { // anonymous
// jichi 12/17/2013: Copied from int TextHook::GetLength(DWORD base, DWORD in)
int GetHookDataLength(const HookParam &hp, DWORD base, DWORD in)
{
  if (CC_UNLIKELY(!base))
    return 0;
  int len;
  switch (hp.length_offset) {
  default: // jichi 12/26/2013: I should not put this default branch to the end
    len = *((int *)base + hp.length_offset);
    if (len >= 0) {
      if (hp.type & USING_UNICODE)
        len <<= 1;
      break;
    }
    else if (len != -1)
      break;
    //len == -1 then continue to case 0.
  case 0:
    if (hp.type & USING_UNICODE)
      len = wcslen((LPCWSTR)in) << 1;
    else
      len = strlen((LPCSTR)in);
    break;
  case 1:
    if (hp.type & USING_UNICODE)
      len = 2;
    else {
      if (hp.type & BIG_ENDIAN)
        in >>= 8;
      len = LeadByteTable[in&0xff];  //Slightly faster than IsDBCSLeadByte
    }
    break;
  }
  // jichi 12/25/2013: This function originally return -1 if failed
  //return len;
  return max(0, len);
}
} // unnamed
#endif // DEBUG

namespace { // unnamed helpers

// jichi 8/18/2013: Original maximum relative address in ITH
//enum { MAX_REL_ADDR = 0x200000 };

// jichi 10/1/2013: Increase relative address limit. Certain game engine like Artemis has larger code region
enum { MAX_REL_ADDR = 0x300000 };

static union {
  char text_buffer[0x1000];
  wchar_t wc_buffer[0x800];

  struct { // CodeSection
    DWORD base;
    DWORD size;
  } code_section[0x200];
};

char text_buffer_prev[0x1000];
DWORD buffer_index,
      buffer_length;

BOOL SafeFillRange(LPCWSTR dll, DWORD *lower, DWORD *upper)
{
  BOOL ret = FALSE;
  ITH_WITH_SEH(ret = FillRange(dll, lower, upper));
  return ret;
}

// jichi 3/11/2014: The original FindEntryAligned function could raise exceptions without admin priv
DWORD SafeFindEntryAligned(DWORD start, DWORD back_range)
{
  DWORD ret = 0;
  ITH_WITH_SEH(ret = Util::FindEntryAligned(start, back_range));
  return ret;
}

} // unnamed namespace

namespace Engine {

/********************************************************************************************
KiriKiri hook:
  Usually there are xp3 files in the game folder but also exceptions.
  Find TVP(KIRIKIRI) in the version description is a much more precise way.

  KiriKiri1 correspond to AGTH KiriKiri hook, but this doesn't always work well.
  Find call to GetGlyphOutlineW and go to function header. EAX will point to a
  structure contains character (at 0x14, [EAX+0x14]) we want. To split names into
  different threads AGTH uses [EAX], seems that this value stands for font size.
  Since KiriKiri is compiled by BCC and BCC fastcall uses EAX to pass the first
  parameter. Here we choose EAX is reasonable.
  KiriKiri2 is a redundant hook to catch text when 1 doesn't work. When this happens,
  usually there is a single GetTextExtentPoint32W contains irregular repetitions which
  is out of the scope of KS or KF. This time we find a point and split them into clean
  text threads. First find call to GetTextExtentPoint32W and step out of this function.
  Usually there is a small loop. It is this small loop messed up the text. We can find
  one ADD EBX,2 in this loop. It's clear that EBX is a string pointer goes through the
  string. After the loop EBX will point to the end of the string. So EBX-2 is the last
  char and we insert hook here to extract it.
********************************************************************************************/
#if 0 // jichi 11/12/2013: not used
static void SpecialHookKiriKiri(DWORD esp_base, HookParam *hp, DWORD *data, DWORD *split, DWORD *len)
{
  DWORD p1 =  *(DWORD *)(esp_base - 0x14),
        p2 =  *(DWORD *)(esp_base - 0x18);
  if ((p1>>16) == (p2>>16)) {
    if (DWORD p3 = *(DWORD *)p1) {
      p3 += 8;
      for (p2 = p3 + 2; *(WORD *)p2; p2 += 2);
      *len = p2 - p3;
      *data = p3;
      p1 = *(DWORD *)(esp_base - 0x20);
      p1 = *(DWORD *)(p1 + 0x74);
      *split = p1 | *(DWORD *)(esp_base + 0x48);
    } else
      *len = 0;
  } else
    *len=0;
}
#endif // 0

void FindKiriKiriHook(DWORD fun, DWORD size, DWORD pt, DWORD flag)
{
  //WCHAR str[0x40];
  DWORD sig = flag ? 0x575653 : 0xec8b55;
  DWORD t = 0;
  for (DWORD i = 0x1000; i < size - 4; i++)
    if (*(WORD *)(pt + i) == 0x15ff) {
      DWORD addr = *(DWORD *)(pt + i + 2);
      if (addr >= pt && addr <= pt + size - 4
          && *(DWORD *)addr == fun)
        t++;
      if (t == flag + 1)  // We find call to GetGlyphOutlineW or GetTextExtentPoint32W.
        //swprintf(str, L"CALL addr:0x%.8X",i+pt);
        //ConsoleOutput(str);
        for (DWORD j = i; j > i - 0x1000; j--)
          if (((*(DWORD *)(pt + j)) & 0xffffff) == sig) {
            if (flag)  { // We find the function entry. flag indicate 2 hooks.
              t = 0;  //KiriKiri2, we need to find call to this function.
              for (DWORD k = j + 0x6000; k < j + 0x8000; k++) // Empirical range.
                if (*(BYTE *)(pt + k) == 0xe8) {
                  if (k + 5 + *(DWORD *)(pt + k + 1) == j)
                    t++;
                  if (t == 2) {
                    //for (k+=pt+0x14; *(WORD*)(k)!=0xC483;k++);
                    //swprintf(str, L"Hook addr: 0x%.8X",pt+k);
                    //ConsoleOutput(str);
                    HookParam hp = {};
                    hp.addr = pt + k + 0x14;
                    hp.off = -0x14;
                    hp.ind = -0x2;
                    hp.split = -0xc;
                    hp.length_offset = 1;
                    hp.type |= USING_UNICODE|NO_CONTEXT|USING_SPLIT|DATA_INDIRECT;
                    ConsoleOutput("vnreng: INSERT KiriKiri2");
                    NewHook(hp, L"KiriKiri2");
                    return;
                  }
                }
            } else {
              //swprintf(str, L"Hook addr: 0x%.8X",pt+j);
              //ConsoleOutput(str);
              HookParam hp = {};
              hp.addr = (DWORD)pt + j;
              hp.off = -0x8;
              hp.ind = 0x14;
              hp.split = -0x8;
              hp.length_offset = 1;
              hp.type |= USING_UNICODE|DATA_INDIRECT|USING_SPLIT|SPLIT_INDIRECT;
              ConsoleOutput("vnreng: INSERT KiriKiri1");
              NewHook(hp, L"KiriKiri1");
            }
            return;
          }
        //ConsoleOutput("vnreng:KiriKiri: FAILED to find function entry");
    }
  ConsoleOutput("vnreng:KiriKiri: failed");
}

void InsertKiriKiriHook()
{
  FindKiriKiriHook((DWORD)GetGlyphOutlineW,      module_limit_ - module_base_, module_base_, 0); // KiriKiri1
  FindKiriKiriHook((DWORD)GetTextExtentPoint32W, module_limit_ - module_base_, module_base_, 1); // KiriKiri2
  //RegisterEngineType(ENGINE_KIRIKIRI);
}

/********************************************************************************************
BGI hook:
  Usually game folder contains BGI.*. After first run BGI.gdb appears.

  BGI engine has font caching issue so the strategy is simple.
  First find call to TextOutA or TextOutW then reverse to function entry point,
  until full text is caught.
  After 2 tries we will get to the right place. Use ESP value to split text since
  it's likely to be different for different calls.
********************************************************************************************/
namespace { // unnamed
#if 0 // jichi 12/28/2013: dynamic BGI is not used
static bool FindBGIHook(DWORD fun, DWORD size, DWORD pt, WORD sig)
{
  if (!fun) {
    ConsoleOutput("vnreng:BGI: cannot find BGI hook");
    //swprintf(str, L"Can't find BGI hook: %.8X.",fun);
    //ConsoleOutput(str);
    return false;
  }
  //WCHAR str[0x40];
  //i=FindCallBoth(fun,size,pt);

  //swprintf(str, L"CALL addr: 0x%.8X",pt+i);
  //ConsoleOutput(str);
  for (DWORD i = fun, j = fun; j > i - 0x100; j--)
    if ((*(WORD *)(pt + j)) == sig) { // Fun entry 1.
      //swprintf(str, L"Entry 1: 0x%.8X",pt+j);
      //ConsoleOutput(str);
      for (DWORD k = i + 0x100; k < i+0x800; k++)
        if (*(BYTE *)(pt + k) == 0xe8)
          if (k + 5 + *(DWORD *)(pt + k + 1) == j) { // Find call to fun1.
            //swprintf(str, L"CALL to entry 1: 0x%.8X",pt+k);
            //ConsoleOutput(str);
            for (DWORD l = k; l > k - 0x100;l--)
              if ((*(WORD *)(pt+l))==0xec83) { //Fun entry 2.
                //swprintf(str, L"Entry 2(final): 0x%.8X",pt+l);
                //ConsoleOutput(str);
                HookParam hp = {};
                hp.addr = (DWORD)pt + l;
                hp.off = 0x8;
                hp.split = -0x18;
                hp.length_offset = 1;
                hp.type = BIG_ENDIAN|USING_SPLIT;
                ConsoleOutput("vnreng:INSERT DynamicBGI");
                NewHook(hp, L"BGI");
                return true;
              }
          }
    }
  ConsoleOutput("vnreng:DynamicBGI: failed");
  return false;
}
bool InsertBGIDynamicHook(LPVOID addr, DWORD frame, DWORD stack)
{
  if (addr != TextOutA && addr != TextOutW)  {
    //ConsoleOutput("vnreng:DynamicBGI: failed");
    return false;
  }

  DWORD i = *(DWORD *)(stack + 4) - module_base_;
  return FindBGIHook(i, module_limit_ - module_base_, module_base_, 0xec83);
}
#endif // 0

bool InsertBGI1Hook()
{
  union {
    DWORD i;
    DWORD *id;
    BYTE *ib;
  };
  HookParam hp = {};
  for (i = module_base_ + 0x1000; i < module_limit_; i++) {
    if (ib[0] == 0x3D) {
      i++;
      if (id[0] == 0xffff) { //cmp eax,0xffff
        hp.addr = SafeFindEntryAligned(i, 0x40);
        if (hp.addr) {
          hp.off = 0xc;
          hp.split = -0x18;
          hp.type = BIG_ENDIAN|USING_SPLIT;
          hp.length_offset = 1;
          ConsoleOutput("vnreng:INSERT BGI#1");
          NewHook(hp, L"BGI");
          //RegisterEngineType(ENGINE_BGI);
          return true;
        }
      }
    }
    if (ib[0] == 0x81 && ((ib[1] & 0xf8) == 0xf8)) {
      i += 2;
      if (id[0] == 0xffff) { //cmp reg,0xffff
        hp.addr = SafeFindEntryAligned(i, 0x40);
        if (hp.addr) {
          hp.off = 0xc;
          hp.split = -0x18;
          hp.type = BIG_ENDIAN|USING_SPLIT;
          hp.length_offset = 1;
          ConsoleOutput("vnreng: INSERT BGI#2");
          NewHook(hp, L"BGI");
          //RegisterEngineType(ENGINE_BGI);
          return true;
        }
      }
    }
  }
  //ConsoleOutput("Unknown BGI engine.");

  //ConsoleOutput("Probably BGI. Wait for text.");
  //SwitchTrigger(true);
  //trigger_fun_=InsertBGIDynamicHook;
  ConsoleOutput("vnreng:BGI: failed");
  return false;
}

/**
 *  jichi 2/5/2014: Add an alternative BGI hook
 *
 *  Issue: This hook cannot extract character name for コトバの消えた日
 *
 *  See: http://tieba.baidu.com/p/2845113296
 *  世界と世界の真ん中で
 *  - /HSN4@349E0:sekachu.exe // Disabled BGI3, floating split char
 *  - /HS-1C:-4@68E56 // Not used, cannot detect character name
 *  - /HSC@34C80:sekachu.exe  // BGI2, extract both scenario and character names
 *
 *  [Lump of Sugar] 世界と世界の真ん中で
 *  /HSC@34C80:sekachu.exe
 *  - addr: 216192 = 0x34c80
 *  - module: 3599131534
 *  - off: 12 = 0xc
 *  - type: 65 = 0x41
 *
 *  base: 0x11a0000
 *  hook_addr = base + addr = 0x11d4c80
 *
 *  011D4C7E     CC             INT3
 *  011D4C7F     CC             INT3
 *  011D4C80  /$ 55             PUSH EBP    ; jichi: hook here
 *  011D4C81  |. 8BEC           MOV EBP,ESP
 *  011D4C83  |. 6A FF          PUSH -0x1
 *  011D4C85  |. 68 E6592601    PUSH sekachu.012659E6
 *  011D4C8A  |. 64:A1 00000000 MOV EAX,DWORD PTR FS:[0]
 *  011D4C90  |. 50             PUSH EAX
 *  011D4C91  |. 81EC 300D0000  SUB ESP,0xD30
 *  011D4C97  |. A1 D8C82801    MOV EAX,DWORD PTR DS:[0x128C8D8]
 *  011D4C9C  |. 33C5           XOR EAX,EBP
 *  011D4C9E  |. 8945 F0        MOV DWORD PTR SS:[EBP-0x10],EAX
 *  011D4CA1  |. 53             PUSH EBX
 *  011D4CA2  |. 56             PUSH ESI
 *  011D4CA3  |. 57             PUSH EDI
 *  011D4CA4  |. 50             PUSH EAX
 *  011D4CA5  |. 8D45 F4        LEA EAX,DWORD PTR SS:[EBP-0xC]
 *  011D4CA8  |. 64:A3 00000000 MOV DWORD PTR FS:[0],EAX
 *  011D4CAE  |. 8B4D 0C        MOV ECX,DWORD PTR SS:[EBP+0xC]
 *  011D4CB1  |. 8B55 18        MOV EDX,DWORD PTR SS:[EBP+0x18]
 *  011D4CB4  |. 8B45 08        MOV EAX,DWORD PTR SS:[EBP+0x8]
 *  011D4CB7  |. 8B5D 10        MOV EBX,DWORD PTR SS:[EBP+0x10]
 *  011D4CBA  |. 8B7D 38        MOV EDI,DWORD PTR SS:[EBP+0x38]
 *  011D4CBD  |. 898D D8F3FFFF  MOV DWORD PTR SS:[EBP-0xC28],ECX
 *  011D4CC3  |. 8B4D 28        MOV ECX,DWORD PTR SS:[EBP+0x28]
 *  011D4CC6  |. 8995 9CF3FFFF  MOV DWORD PTR SS:[EBP-0xC64],EDX
 *  011D4CCC  |. 51             PUSH ECX
 *  011D4CCD  |. 8B0D 305C2901  MOV ECX,DWORD PTR DS:[0x1295C30]
 *  011D4CD3  |. 8985 E0F3FFFF  MOV DWORD PTR SS:[EBP-0xC20],EAX
 *  011D4CD9  |. 8B45 1C        MOV EAX,DWORD PTR SS:[EBP+0x1C]
 *  011D4CDC  |. 8D95 4CF4FFFF  LEA EDX,DWORD PTR SS:[EBP-0xBB4]
 *  011D4CE2  |. 52             PUSH EDX
 *  011D4CE3  |. 899D 40F4FFFF  MOV DWORD PTR SS:[EBP-0xBC0],EBX
 *  011D4CE9  |. 8985 1CF4FFFF  MOV DWORD PTR SS:[EBP-0xBE4],EAX
 *  011D4CEF  |. 89BD F0F3FFFF  MOV DWORD PTR SS:[EBP-0xC10],EDI
 *  011D4CF5  |. E8 862EFDFF    CALL sekachu.011A7B80
 *  011D4CFA  |. 33C9           XOR ECX,ECX
 *  011D4CFC  |. 8985 60F3FFFF  MOV DWORD PTR SS:[EBP-0xCA0],EAX
 *  011D4D02  |. 3BC1           CMP EAX,ECX
 *  011D4D04  |. 0F84 0F1C0000  JE sekachu.011D6919
 *  011D4D0A  |. E8 31F6FFFF    CALL sekachu.011D4340
 *  011D4D0F  |. E8 6CF8FFFF    CALL sekachu.011D4580
 *  011D4D14  |. 8985 64F3FFFF  MOV DWORD PTR SS:[EBP-0xC9C],EAX
 *  011D4D1A  |. 8A03           MOV AL,BYTE PTR DS:[EBX]
 *  011D4D1C  |. 898D 90F3FFFF  MOV DWORD PTR SS:[EBP-0xC70],ECX
 *  011D4D22  |. 898D 14F4FFFF  MOV DWORD PTR SS:[EBP-0xBEC],ECX
 *  011D4D28  |. 898D 38F4FFFF  MOV DWORD PTR SS:[EBP-0xBC8],ECX
 *  011D4D2E  |. 8D71 01        LEA ESI,DWORD PTR DS:[ECX+0x1]
 *  011D4D31  |. 3C 20          CMP AL,0x20
 *  011D4D33  |. 7D 75          JGE SHORT sekachu.011D4DAA
 *  011D4D35  |. 0FBEC0         MOVSX EAX,AL
 *  011D4D38  |. 83C0 FE        ADD EAX,-0x2                             ;  Switch (cases 2..8)
 *  011D4D3B  |. 83F8 06        CMP EAX,0x6
 *  011D4D3E  |. 77 6A          JA SHORT sekachu.011D4DAA
 *  011D4D40  |. FF2485 38691D0>JMP DWORD PTR DS:[EAX*4+0x11D6938]
 */
bool InsertBGI2Hook()
{
  const BYTE ins[] = {
    0x3c, 0x20,      // 011d4d31  |. 3c 20          cmp al,0x20
    0x7d, 0x75,      // 011d4d33  |. 7d 75          jge short sekachu.011d4daa
    0x0f,0xbe,0xc0,  // 011d4d35  |. 0fbec0         movsx eax,al
    0x83,0xc0, 0xfe, // 011d4d38  |. 83c0 fe        add eax,-0x2                             ;  switch (cases 2..8)
    0x83,0xf8, 0x06, // 011d4d3b  |. 83f8 06        cmp eax,0x6
    0x77, 0x6a       // 011d4d3e  |. 77 6a          ja short sekachu.011d4daa
  };
  enum { hook_offset = 0x34c80 - 0x34d31 }; // distance to the beginning of the function
  ULONG range = min(module_limit_ - module_base_, MAX_REL_ADDR);
  ULONG reladdr = SearchPattern(module_base_, range, ins, sizeof(ins));
  //ITH_GROWL_DWORD(reladdr);
  if (!reladdr) {
    ConsoleOutput("vnreng:BGI2: pattern not found");
    return false;
  }
  ULONG addr = module_base_ + reladdr + hook_offset;
  enum : BYTE { push_ebp = 0x55 };  // 011d4c80  /$ 55             push ebp
  if (*(BYTE *)addr != push_ebp) {
    ConsoleOutput("vnreng:BGI2: pattern found but the function offset is invalid");
    return false;
  }

  HookParam hp = {};
  hp.type = USING_STRING;
  hp.off = 0xc;
  hp.addr = addr;

  //ITH_GROWL_DWORD2(hp.addr, module_base_);

  ConsoleOutput("vnreng: INSERT BGI2");
  NewHook(hp, L"BGI2");
  return true;
}

#if 0
/**
 *  jichi 1/31/2014: Add a new BGI hook
 *  See: http://www.hongfire.com/forum/showthread.php/36807-AGTH-text-extraction-tool-for-games-translation/page702
 *  See: http://www.hongfire.com/forum/showthread.php/36807-AGTH-text-extraction-tool-for-games-translation/page716
 *
 *  Issue: This hook has floating split char
 *
 *  [ぷちけろ] コトバの消えた日 ～心まで裸にする純愛調教～体験版
 *  /HS-1C:-4@68E56:BGI.exe
 *  - addr: 429654 (0x68e56)
 *  - module: 3927275266 (0xea157702)
 *  - off: 4294967264 = 0xffffffe0 = -0x20
 *  - split: 4294967288 = 0xfffffff8 = -0x8
 *  - type: 81 = 0x51
 *
 *  00E88E3D     CC             INT3
 *  00E88E3E     CC             INT3
 *  00E88E3F     CC             INT3
 *  00E88E40  /. 55             PUSH EBP
 *  00E88E41  |. 8BEC           MOV EBP,ESP
 *  00E88E43  |. 56             PUSH ESI
 *  00E88E44  |. 57             PUSH EDI
 *  00E88E45  |. 8B7D 08        MOV EDI,DWORD PTR SS:[EBP+0x8]
 *  00E88E48  |. 57             PUSH EDI
 *  00E88E49  |. E8 C28A0100    CALL BGI.00EA1910
 *  00E88E4E  |. 57             PUSH EDI                                 ; |Arg1
 *  00E88E4F  |. 8BF0           MOV ESI,EAX                              ; |
 *  00E88E51  |. E8 BA8A0100    CALL BGI.00EA1910                        ; \BGI.00EA1910
 *  00E88E56  |. 83C4 08        ADD ESP,0x8 ; jichi: hook here
 *  00E88E59  |. 2BC6           SUB EAX,ESI
 *  00E88E5B  |. EB 03          JMP SHORT BGI.00E88E60
 *  00E88E5D  |  8D49 00        LEA ECX,DWORD PTR DS:[ECX]
 *  00E88E60  |> 8A0E           /MOV CL,BYTE PTR DS:[ESI]
 *  00E88E62  |. 880C30         |MOV BYTE PTR DS:[EAX+ESI],CL
 *  00E88E65  |. 46             |INC ESI
 *  00E88E66  |. 84C9           |TEST CL,CL
 *  00E88E68  |.^75 F6          \JNZ SHORT BGI.00E88E60
 *  00E88E6A  |. 5F             POP EDI
 *  00E88E6B  |. 33C0           XOR EAX,EAX
 *  00E88E6D  |. 5E             POP ESI
 *  00E88E6E  |. 5D             POP EBP
 *  00E88E6F  \. C3             RETN
 */
bool InsertBGI3Hook()
{
  const BYTE ins[] = {
    0x83,0xc4, 0x08,// 00e88e56  |. 83c4 08        add esp,0x8 ; hook here
    0x2b,0xc6,      // 00e88e59  |. 2bc6           sub eax,esi
    0xeb, 0x03,     // 00e88e5b  |. eb 03          jmp short bgi.00e88e60
    0x8d,0x49, 0x00,// 00e88e5d  |  8d49 00        lea ecx,dword ptr ds:[ecx]
    0x8a,0x0e,      // 00e88e60  |> 8a0e           /mov cl,byte ptr ds:[esi]
    0x88,0x0c,0x30, // 00e88e62  |. 880c30         |mov byte ptr ds:[eax+esi],cl
    0x46,           // 00e88e65  |. 46             |inc esi
    0x84,0xc9,      // 00e88e66  |. 84c9           |test cl,cl
    0x75, 0xf6      // 00e88e68  |.^75 f6          \jnz short bgi.00e88e60
    //0x5f,           // 00e88e6a  |. 5f             pop edi
    //0x33,0xc0,      // 00e88e6b  |. 33c0           xor eax,eax
    //0x5e,           // 00e88e6d  |. 5e             pop esi
    //0x5d,           // 00e88e6e  |. 5d             pop ebp
    //0xc3            // 00e88e6f  \. c3             retn
  };
  enum { hook_offset = 0 };
  ULONG range = min(module_limit_ - module_base_, MAX_REL_ADDR);
  ULONG reladdr = SearchPattern(module_base_, range, ins, sizeof(ins));
  //reladdr = 0x68e56;
  if (!reladdr) {
    ConsoleOutput("vnreng:BGI3: pattern not found");
    return false;
  }

  HookParam hp = {};
  hp.type = USING_STRING|USING_SPLIT;
  hp.off = -0x20;
  hp.split = -0x8;
  hp.addr = module_base_ + reladdr + hook_offset;

  //ITH_GROWL_DWORD2(hp.addr, module_base_);

  ConsoleOutput("vnreng: INSERT BGI3");
  NewHook(hp, L"BGI3");
  return true;
}
#endif // 0
} // unnamed

// jichi 1/31/2014: Insert both hooks since I am not sure if BG1 games also have BG2 patterns
// The BG1 hook is harmless to BG2 any way
bool InsertBGIHook()
{
  bool b1 = InsertBGI1Hook(),
       b2 = InsertBGI2Hook();
  return b1 || b2; // prevent conditional shortcut
}

/********************************************************************************************
Reallive hook:
  Process name is reallive.exe or reallive*.exe.

  Technique to find Reallive hook is quite different from 2 above.
  Usually Reallive engine has a font caching issue. This time we wait
  until the first call to GetGlyphOutlineA. Reallive engine usually set
  up stack frames so we can just refer to EBP to find function entry.

********************************************************************************************/
bool InsertRealliveDynamicHook(LPVOID addr, DWORD frame, DWORD stack)
{
  if (addr != GetGlyphOutlineA)
    return false;
  if (DWORD i = frame) {
    i = *(DWORD *)(i + 4);
    for (DWORD j = i; j > i - 0x100; j--)
      if (*(WORD *)j==0xec83) {
        HookParam hp = {};
        hp.addr = j;
        hp.off = 0x14;
        hp.split = -0x18;
        hp.length_offset = 1;
        hp.type |= BIG_ENDIAN|USING_SPLIT;
        NewHook(hp, L"RealLive");
        //RegisterEngineType(ENGINE_REALLIVE);
        return true;;
      }
  }
  return true; // jichi 12/25/2013: return true
}
void InsertRealliveHook()
{
  //ConsoleOutput("Probably Reallive. Wait for text.");
  ConsoleOutput("vnreng: TRIGGER Reallive");
  trigger_fun_ = InsertRealliveDynamicHook;
  SwitchTrigger(true);
}
/**
 *  jichi 8/17/2013:  SiglusEngine from siglusengine.exe
 *  The old hook does not work for new games.
 *  The new hook cannot recognize character names.
 *  Insert old first. As the pattern could also be found in the old engine.
 */

/**
 *  jichi 8/16/2013: Insert new siglus hook
 *  See (CaoNiMaGeBi): http://tieba.baidu.com/p/2531786952
 *  Issue: floating text
 *  Example:
 *  0153588b9534fdffff8b43583bd7
 *  0153 58          add dword ptr ds:[ebx+58],edx
 *  8b95 34fdffff    mov edx,dword ptr ss:[ebp-2cc]
 *  8b43 58          mov eax,dword ptr ds:[ebx+58]
 *  3bd7             cmp edx,edi    ; hook here
 *
 *  /HW-1C@D9DB2:SiglusEngine.exe
 *  - addr: 892338 (0xd9db2)
 *  - extern_fun: 0x0
 *  - function: 0
 *  - hook_len: 0
 *  - ind: 0
 *  - length_offset: 1
 *  - module: 356004490 (0x1538328a)
 *  - off: 4294967264 (0xffffffe0L, 0x-20)
 *  - recover_len: 0
 *  - split: 0
 *  - split_ind: 0
 *  - type: 66   (0x42)
 */
bool InsertSiglus2Hook()
{
  //const BYTE ins[] = { // size = 14
  //  0x01,0x53, 0x58,                // 0153 58          add dword ptr ds:[ebx+58],edx
  //  0x8b,0x95, 0x34,0xfd,0xff,0xff, // 8b95 34fdffff    mov edx,dword ptr ss:[ebp-2cc]
  //  0x8b,0x43, 0x58,                // 8b43 58          mov eax,dword ptr ds:[ebx+58]
  //  0x3b,0xd7                       // 3bd7             cmp edx,edi ; hook here
  //};
  //enum { cur_ins_size = 2 };
  //enum { hook_offset = sizeof(ins) - cur_ins_size }; // = 14 - 2  = 12, current inst is the last one
  const BYTE ins[] = {
    0x3b,0xd7,  // cmp edx,edi ; hook here
    0x75,0x4b   // jnz short
  };
  enum { hook_offset = 0 };
  ULONG range = min(module_limit_ - module_base_, MAX_REL_ADDR);
  ULONG reladdr = SearchPattern(module_base_, range, ins, sizeof(ins));
  if (!reladdr) {
    ConsoleOutput("vnreng:Siglus2: pattern not found");
    //ConsoleOutput("Not SiglusEngine2");
    return false;
  }

  HookParam hp = {};
  hp.type = USING_UNICODE; //|NO_CONTEXT; // jichi 3/8/2014: Use no_context to prevent floating threads.
  hp.length_offset = 1;
  hp.off = -0x20;
  hp.addr = module_base_ + reladdr + hook_offset;

  //index = SearchPattern(module_base_, size,ins, sizeof(ins));
  //ITH_GROWL_DWORD2(base, index);

  ConsoleOutput("vnreng: INSERT Siglus2");
  NewHook(hp, L"SiglusEngine2");
  //ConsoleOutput("SiglusEngine2");
  return true;
}

static void SpecialHookSiglus(DWORD esp_base, HookParam* hp, DWORD* data, DWORD* split, DWORD* len)
{
  __asm
  {
    mov edx,esp_base
    mov ecx,[edx-0xc]
    mov eax,[ecx+0x14]
    add ecx,4
    cmp eax,0x8
    cmovnb ecx,[ecx]
    mov edx,len
    add eax,eax
    mov [edx],eax
    mov edx,data
    mov [edx],ecx
  }
}

// jichi: 8/17/2013: Change return type to bool
bool InsertSiglus1Hook()
{
  //const BYTE ins[8]={0x33,0xC0,0x8B,0xF9,0x89,0x7C,0x24}; // jichi 8/18/2013: wrong count?
  const BYTE ins[]={0x33,0xc0,0x8b,0xf9,0x89,0x7c,0x24};
  ULONG range = max(module_limit_ - module_base_, MAX_REL_ADDR);
  ULONG reladdr = SearchPattern(module_base_, range, ins, sizeof(ins));
  if (!reladdr) { // jichi 8/17/2013: Add "== 0" check to prevent breaking new games
    //ConsoleOutput("Unknown SiglusEngine");
    ConsoleOutput("vnreng:Siglus: pattern not found");
    return false;
  }

  DWORD base = module_base_ + reladdr;
  DWORD limit = base - 0x100;
  while (base > limit) {
    if (*(WORD*)base == 0xff6a) {
      HookParam hp = {};
      hp.addr = base;
      hp.extern_fun = SpecialHookSiglus;
      hp.type= EXTERN_HOOK|USING_UNICODE;
      ConsoleOutput("vnreng: INSERT Siglus");
      NewHook(hp, L"SiglusEngine");
      //RegisterEngineType(ENGINE_SIGLUS);
      return true;
    }
    base--;
  }
  ConsoleOutput("vnreng:Siglus: failed");
  return false;
}

// jichi 8/17/2013: Insert old first. As the pattern could also be found in the old engine.
bool InsertSiglusHook()
{ return InsertSiglus1Hook() || InsertSiglus2Hook(); }

/********************************************************************************************
MAJIRO hook:
  Game folder contains both data.arc and scenario.arc. arc files is
  quite common seen so we need 2 files to confirm it's MAJIRO engine.

  Font caching issue. Find call to TextOutA and the function entry.

  The original Majiro hook will catch furiga mixed with the text.
  To split them out we need to find a parameter. Seems there's no
  simple way to handle this case.
  At the function entry, EAX seems to point to a structure to describe
  current  drawing context. +28 seems to be font size. +48 is negative
  if furigana. I don't know exact meaning of this structure,
  just do memory comparisons and get the value working for current release.

********************************************************************************************/
static void SpecialHookMajiro(DWORD esp_base, HookParam* hp, DWORD* data, DWORD* split, DWORD* len)
{
  __asm
  {
    mov edx,esp_base
    mov edi,[edx+0xC]
    mov eax,data
    mov [eax],edi
    or ecx,0xFFFFFFFF
    xor eax,eax
    repne scasb
    not ecx
    dec ecx
    mov eax,len
    mov [eax],ecx
    mov eax,[edx+4]
    mov edx,[eax+0x28]
    mov eax,[eax+0x48]
    sar eax,0x1F
    mov dh,al
    mov ecx,split
    mov [ecx],edx
  }
}
bool InsertMajiroHook()
{
  DWORD addr = Util::FindCallAndEntryAbs((DWORD)TextOutA,module_limit_-module_base_,module_base_,0xec81);
  if (!addr) {
    ConsoleOutput("vnreng:MAJIRO: failed");
    return false;
  }

  HookParam hp = {};
  //hp.off=0xC;
  //hp.split=4;
  //hp.split_ind=0x28;
  //hp.type|=USING_STRING|USING_SPLIT|SPLIT_INDIRECT;
  hp.addr = addr;
  hp.type = EXTERN_HOOK;
  hp.extern_fun = SpecialHookMajiro;
  ConsoleOutput("vnreng: INSERT MAJIRO");
  NewHook(hp, L"MAJIRO");
  //RegisterEngineType(ENGINE_MAJIRO);
  return true;
}

/********************************************************************************************
CMVS hook:
  Process name is cmvs.exe or cnvs.exe or cmvs*.exe. Used by PurpleSoftware games.

  Font caching issue. Find call to GetGlyphOutlineA and the function entry.
********************************************************************************************/

namespace { // anonymous

// jichi 3/6/2014: This is the original CMVS hook in ITH
// It does not work for パープルソフトウェア games after しあわせ家族部 (2012)
bool InsertCMVS1Hook()
{
  DWORD addr = Util::FindCallAndEntryAbs((DWORD)GetGlyphOutlineA, module_limit_ - module_base_, module_base_, 0xec83);
  if (!addr) {
    ConsoleOutput("vnreng:CMVS1: failed");
    return false;
  }
  HookParam hp = {};
  hp.addr = addr;
  hp.off = 0x8;
  hp.split = -0x18;
  hp.type = BIG_ENDIAN|USING_SPLIT;
  hp.length_offset= 1 ;

  ConsoleOutput("vnreng: INSERT CMVS1");
  NewHook(hp, L"CMVS");
  //RegisterEngineType(ENGINE_CMVS);
  return true;
}

/**
 *  CMSV
 *  Sample games:
 *  ハピメア: /HAC@48FF3:cmvs32.exe
 *  ハピメアFD: /HB-1C*0@44EE95
 *
 *  Optional: ハピメアFD: /HB-1C*0@44EE95
 *  This hook has issue that the text will be split to a large amount of threads
 *  - length_offset: 1
 *  - off: 4294967264 = 0xffffffe0 = -0x20
 *  - type: 8
 *
 *  ハピメア: /HAC@48FF3:cmvs32.exe
 *  base: 0x400000
 *  - length_offset: 1
 *  - off: 12 = 0xc
 *  - type: 68 = 0x44
 *
 *  00448fee     cc             int3
 *  00448fef     cc             int3
 *  00448ff0  /$ 55             push ebp
 *  00448ff1  |. 8bec           mov ebp,esp
 *  00448ff3  |. 83ec 68        sub esp,0x68 ; jichi: hook here
 *  00448ff6  |. 8b01           mov eax,dword ptr ds:[ecx]
 *  00448ff8  |. 56             push esi
 *  00448ff9  |. 33f6           xor esi,esi
 *  00448ffb  |. 33d2           xor edx,edx
 *  00448ffd  |. 57             push edi
 *  00448ffe  |. 894d fc        mov dword ptr ss:[ebp-0x4],ecx
 *  00449001  |. 3bc6           cmp eax,esi
 *  00449003  |. 74 37          je short cmvs32.0044903c
 *  00449005  |> 66:8b78 08     /mov di,word ptr ds:[eax+0x8]
 *  00449009  |. 66:3b7d 0c     |cmp di,word ptr ss:[ebp+0xc]
 *  0044900d  |. 75 0a          |jnz short cmvs32.00449019
 *  0044900f  |. 66:8b7d 10     |mov di,word ptr ss:[ebp+0x10]
 *  00449013  |. 66:3978 0a     |cmp word ptr ds:[eax+0xa],di
 *  00449017  |. 74 0a          |je short cmvs32.00449023
 *  00449019  |> 8bd0           |mov edx,eax
 *  0044901b  |. 8b00           |mov eax,dword ptr ds:[eax]
 *  0044901d  |. 3bc6           |cmp eax,esi
 *  0044901f  |.^75 e4          \jnz short cmvs32.00449005
 *  00449021  |. eb 19          jmp short cmvs32.0044903c
 *  00449023  |> 3bd6           cmp edx,esi
 *  00449025  |. 74 0a          je short cmvs32.00449031
 *  00449027  |. 8b38           mov edi,dword ptr ds:[eax]
 *  00449029  |. 893a           mov dword ptr ds:[edx],edi
 *  0044902b  |. 8b11           mov edx,dword ptr ds:[ecx]
 *  0044902d  |. 8910           mov dword ptr ds:[eax],edx
 *  0044902f  |. 8901           mov dword ptr ds:[ecx],eax
 *  00449031  |> 8b40 04        mov eax,dword ptr ds:[eax+0x4]
 *  00449034  |. 3bc6           cmp eax,esi
 *  00449036  |. 0f85 64010000  jnz cmvs32.004491a0
 *  0044903c  |> 8b55 08        mov edx,dword ptr ss:[ebp+0x8]
 *  0044903f  |. 53             push ebx
 *  00449040  |. 0fb75d 0c      movzx ebx,word ptr ss:[ebp+0xc]
 *  00449044  |. b8 00000100    mov eax,0x10000
 *  00449049  |. 8945 e4        mov dword ptr ss:[ebp-0x1c],eax
 *  0044904c  |. 8945 f0        mov dword ptr ss:[ebp-0x10],eax
 *  0044904f  |. 8d45 e4        lea eax,dword ptr ss:[ebp-0x1c]
 *  00449052  |. 50             push eax                                 ; /pMat2
 *  00449053  |. 56             push esi                                 ; |Buffer
 *  00449054  |. 56             push esi                                 ; |BufSize
 *  00449055  |. 8d4d d0        lea ecx,dword ptr ss:[ebp-0x30]          ; |
 *  00449058  |. 51             push ecx                                 ; |pMetrics
 *  00449059  |. 6a 05          push 0x5                                 ; |Format = GGO_GRAY4_BITMAP
 *  0044905b  |. 53             push ebx                                 ; |Char
 *  0044905c  |. 52             push edx                                 ; |hDC
 *  0044905d  |. 8975 e8        mov dword ptr ss:[ebp-0x18],esi          ; |
 *  00449060  |. 8975 ec        mov dword ptr ss:[ebp-0x14],esi          ; |
 *  00449063  |. ff15 5cf05300  call dword ptr ds:[<&gdi32.getglyphoutli>; \GetGlyphOutlineA ; jichi 3/7/2014: Should I hook here?
 *  00449069  |. 8b75 10        mov esi,dword ptr ss:[ebp+0x10]
 *  0044906c  |. 0faff6         imul esi,esi
 *  0044906f  |. 8bf8           mov edi,eax
 *  00449071  |. 8d04bd 0000000>lea eax,dword ptr ds:[edi*4]
 *  00449078  |. 3bc6           cmp eax,esi
 *  0044907a  |. 76 02          jbe short cmvs32.0044907e
 *  0044907c  |. 8bf0           mov esi,eax
 *  0044907e  |> 56             push esi                                 ; /Size
 *  0044907f  |. 6a 00          push 0x0                                 ; |Flags = LMEM_FIXED
 *  00449081  |. ff15 34f25300  call dword ptr ds:[<&kernel32.localalloc>; \LocalAlloc
 */
bool InsertCMVS2Hook()
{
  // There are multiple functions satisfy the pattern below.
  // Hook to any one of them is OK.
  const BYTE ins[] = {  // function begin
    0x55,               // 00448ff0  /$ 55             push ebp
    0x8b,0xec,          // 00448ff1  |. 8bec           mov ebp,esp
    0x83,0xec, 0x68,    // 00448ff3  |. 83ec 68        sub esp,0x68 ; jichi: hook here
    0x8b,0x01,          // 00448ff6  |. 8b01           mov eax,dword ptr ds:[ecx]
    0x56,               // 00448ff8  |. 56             push esi
    0x33,0xf6,          // 00448ff9  |. 33f6           xor esi,esi
    0x33,0xd2,          // 00448ffb  |. 33d2           xor edx,edx
    0x57,               // 00448ffd  |. 57             push edi
    0x89,0x4d, 0xfc,    // 00448ffe  |. 894d fc        mov dword ptr ss:[ebp-0x4],ecx
    0x3b,0xc6,          // 00449001  |. 3bc6           cmp eax,esi
    0x74, 0x37          // 00449003  |. 74 37          je short cmvs32.0044903c
  };
  enum { hook_offset = 3 }; // offset from the beginning of the function
  ULONG range = min(module_limit_ - module_base_, MAX_REL_ADDR);
  ULONG reladdr = SearchPattern(module_base_, range, ins, sizeof(ins));
  if (!reladdr) {
    ConsoleOutput("vnreng:CMVS2: pattern not found");
    return false;
  }

  //reladdr = 0x48ff0;
  //reladdr = 0x48ff3;
  HookParam hp = {};
  hp.addr = module_base_ + reladdr + hook_offset;
  hp.off = 0xc;
  hp.type = BIG_ENDIAN;
  hp.length_offset= 1 ;

  ConsoleOutput("vnreng: INSERT CMVS2");
  NewHook(hp, L"CMVS2");
  return true;
}

} // anonymous namespace

// jichi 3/7/2014: Insert the old hook first since GetGlyphOutlineA can NOT be found in new games
bool InsertCMVSHook()
{
  // Both CMVS1 and CMVS2 exists in new games.
  // Insert the CMVS2 first. Since CMVS1 could break CMVS2
  // And the CMVS1 games do not have CMVS2 patterns.
  return InsertCMVS2Hook() || InsertCMVS1Hook();
}

/********************************************************************************************
rUGP hook:
  Process name is rugp.exe. Used by AGE/GIGA games.

  Font caching issue. Find call to GetGlyphOutlineA and keep stepping out functions.
  After several tries we comes to address in rvmm.dll and everything is catched.
  We see CALL [E*X+0x*] while EBP contains the character data.
  It's not as simple to reverse in rugp at run time as like reallive since rugp dosen't set
  up stack frame. In other words EBP is used for other purpose. We need to find alternative
  approaches.
  The way to the entry of that function gives us clue to find it. There is one CMP EBP,0x8140
  instruction in this function and that's enough! 0x8140 is the start of SHIFT-JIS
  characters. It's determining if ebp contains a SHIFT-JIS character. This function is not likely
  to be used in other ways. We simply search for this instruction and place hook around.
********************************************************************************************/
static void SpecialHookRUGP(DWORD esp_base, HookParam* hp, DWORD* data, DWORD* split, DWORD* len)
{
  DWORD* stack = (DWORD*)esp_base;
  DWORD i,val;
  for (i = 0; i < 4; i++)
  {
    val = *stack++;
    if ((val>>16) == 0) break;

  }
  if (i < 4)
  {
    hp->off = i << 2;
    *data = val;
    *len = 2;
    hp->extern_fun = 0;
    hp->type &= ~EXTERN_HOOK;
  }
  else
  {
    *len = 0;
  }
}

// jichi 10/1/2013: Change return type to bool
bool InsertRUGPHook()
{
  DWORD low, high;
  if (!SafeFillRange(L"rvmm.dll", &low, &high))
    return false;
  //WCHAR str[0x40];
  LPVOID ch = (LPVOID)0x8140;
  enum { range = 0x20000 };
  DWORD t = SearchPattern(low + range, high - low - range, &ch, 4) + range;
  BYTE *s = (BYTE *)(low + t);
  //if (t) {
  if (t != range) { // jichi 10/1/2013: Changed to compare with 0x20000
    if (*(s - 2) != 0x81)
      return false;
    if (DWORD i = SafeFindEntryAligned((DWORD)s, 0x200)) {
      HookParam hp = {};
      hp.addr = i;
      //hp.off= -8;
      hp.length_offset = 1;
      hp.extern_fun = SpecialHookRUGP;
      hp.type |= BIG_ENDIAN|EXTERN_HOOK;
      ConsoleOutput("vnreng: INSERT rUGP#1");
      NewHook(hp, L"rUGP");
      return true;
    }
  } else {
    t = SearchPattern(low, range, &s, 4);
    if (!t) {
      ConsoleOutput("vnreng:rUGP: pattern not found");
      //ConsoleOutput("Can't find characteristic instruction.");
      return false;
    }

    s = (BYTE *)(low + t);
    for (int i = 0; i < 0x200; i++, s--)
      if (s[0] == 0x90
          && *(DWORD *)(s - 3) == 0x90909090) {
        t = low+ t - i + 1;
        //swprintf(str, L"HookAddr 0x%.8x", t);
        //ConsoleOutput(str);
        HookParam hp = {};
        hp.addr = t;
        hp.off = 0x4;
        hp.length_offset = 1;
        hp.type |= BIG_ENDIAN;
        ConsoleOutput("vnreng:INSERT rUGP#2");
        NewHook(hp, L"rUGP");
        //RegisterEngineType(ENGINE_RUGP);
        return true;
      }
  }
  ConsoleOutput("vnreng:rUGP: failed");
  return false;
//rt:
  //ConsoleOutput("Unknown rUGP engine.");
}
/********************************************************************************************
Lucifen hook:
  Game folder contains *.lpk. Used by Navel games.
  Hook is same to GetTextExtentPoint32A, use ESP to split name.
********************************************************************************************/
void InsertLucifenHook()
{
  HookParam hp = {};
  hp.addr = (DWORD)GetTextExtentPoint32A;
  hp.off = 8;
  hp.split = -0x18;
  hp.length_offset = 3;
  hp.type = USING_STRING|USING_SPLIT;
  ConsoleOutput("vnreng: INSERT Lucifen");
  NewHook(hp, L"Lucifen");
  //RegisterEngineType(ENGINE_LUCIFEN);
}
/********************************************************************************************
System40 hook:
  System40 is a game engine developed by Alicesoft.
  Afaik, there are 2 very different types of System40. Each requires a particular hook.

  Pattern 1: Either SACTDX.dll or SACT2.dll exports SP_TextDraw.
  The first relative call in this function draw text to some surface.
  Text pointer is return by last absolute indirect call before that.
  Split parameter is a little tricky. The first register pushed onto stack at the begining
  usually is used as font size later. According to instruction opcode map, push
  eax -- 50, ecx -- 51, edx -- 52, ebx --53, esp -- 54, ebp -- 55, esi -- 56, edi -- 57
  Split parameter value:
  eax - -8,   ecx - -C,  edx - -10, ebx - -14, esp - -18, ebp - -1C, esi - -20, edi - -24
  Just extract the low 4 bit and shift left 2 bit, then minus by -8,
  will give us the split parameter. e.g. push ebx 53->3 *4->C, -8-C=-14.
  Sometimes if split function is enabled, ITH will split text spoke by different
  character into different thread. Just open hook dialog and uncheck split parameter.
  Then click modify hook.

  Pattern 2: *engine.dll exports SP_SetTextSprite.
  At the entry point, EAX should be a pointer to some structure, character at +0x8.
  Before calling this function, the caller put EAX onto stack, we can also find this
  value on stack. But seems parameter order varies from game release. If a future
  game breaks the EAX rule then we need to disassemble the caller code to determine
  data offset dynamically.
********************************************************************************************/

void InsertAliceHook1(DWORD addr, DWORD module, DWORD limit)
{
  if (!addr) {
    ConsoleOutput("vnreng:AliceHook1: failed");
    return;
  }
  for (DWORD i = addr, s = addr; i < s + 0x100; i++)
    if (*(BYTE *)i==0xe8) { // Find the first relative call.
      DWORD j = i + 5 + *(DWORD *)(i + 1);
      if (j > module && j < limit) {
        while (true) { // Find the first register push onto stack.
          DWORD c = disasm((BYTE*)s);
          if (c == 1)
            break;
          s += c;
        }
        DWORD c = *(BYTE *)s;
        HookParam hp = {};
        hp.addr = j;
        hp.off = -0x8;
        hp.split = -8 -((c & 0xf) << 2);
        hp.type = USING_STRING|USING_SPLIT;
        //if (s>j) hp.type^=USING_SPLIT;
        ConsoleOutput("vnreng: INSERT AliceHook1");
        NewHook(hp, L"System40");
        //RegisterEngineType(ENGINE_SYS40);
        return;
      }
    }
  ConsoleOutput("vnreng:AliceHook1: failed");
}
void InsertAliceHook2(DWORD addr)
{
  if (!addr) {
    ConsoleOutput("vnreng:AliceHook2: failed");
    return;
  }
  HookParam hp = {};
  hp.addr = addr;
  hp.off = -0x8;
  hp.ind = 0x8;
  hp.length_offset = 1;
  hp.type = DATA_INDIRECT;
  ConsoleOutput("vnreng: INSERT AliceHook2");
  NewHook(hp, L"System40");
  //RegisterEngineType(ENGINE_SYS40);
}

// jichi 8/23/2013 Move here from engine.cc
// Do not work for the latest Alice games
bool InsertAliceHook()
{
  DWORD low, high, addr;
  if (::GetFunctionAddr("SP_TextDraw", &addr, &low, &high, 0) && addr) {
    InsertAliceHook1(addr, low, low + high);
    return true;
  }
  if (::GetFunctionAddr("SP_SetTextSprite", &addr, &low, &high, 0) && addr) {
    InsertAliceHook2(addr);
    return true;
  }
  ConsoleOutput("vnreng:AliceHook: failed");
  return false;
}

/**
 *  jichi 12/26/2013: Rance hook
 *
 *  ランス01 光をもとめて: /HSN4:-14@5506A9
 *  - addr: 5572265 (0x5596a9)
 *  - off: 4
 *  - split: 4294967272 (0xffffffe8 = -0x18)
 *  - type: 1041 (0x411)
 *
 *    The above code has the same pattern except int3.
 *    005506A9  |. E8 F2FB1600    CALL Rance01.006C02A0 ; hook here
 *    005506AE  |. 83C4 0C        ADD ESP,0xC
 *    005506B1  |. 5F             POP EDI
 *    005506B2  |. 5E             POP ESI
 *    005506B3  |. B0 01          MOV AL,0x1
 *    005506B5  |. 5B             POP EBX
 *    005506B6  \. C2 0400        RETN 0x4
 *    005506B9     CC             INT3
 *
 *  ランス・クエスト: /HSN4:-14@42E08A
 *    0042E08A  |. E8 91ED1F00    CALL RanceQue.0062CE20 ; hook here
 *    0042E08F  |. 83C4 0C        ADD ESP,0xC
 *    0042E092  |. 5F             POP EDI
 *    0042E093  |. 5E             POP ESI
 *    0042E094  |. B0 01          MOV AL,0x1
 *    0042E096  |. 5B             POP EBX
 *    0042E097  \. C2 0400        RETN 0x4
 *    0042E09A     CC             INT3
 */
bool InsertSystem43Hook()
{
  const BYTE ins[] = {  //   005506a9  |. e8 f2fb1600    call rance01.006c02a0 ; hook here
    0x83,0xc4, 0x0c,    //   005506ae  |. 83c4 0c        add esp,0xc
    0x5f,               //   005506b1  |. 5f             pop edi
    0x5e,               //   005506b2  |. 5e             pop esi
    0xb0, 0x01,         //   005506b3  |. b0 01          mov al,0x1
    0x5b,               //   005506b5  |. 5b             pop ebx
    0xc2, 0x04,0x00,    //   005506b6  \. c2 0400        retn 0x4
    0xcc, 0xcc // patching a few int3 to make sure that this is at the end of the code block
  };
  enum { hook_offset = -5 }; // the function call before the ins
  ULONG addr = module_base_; //- sizeof(ins);
  //addr = 0x5506a9;
  do {
    //addr += sizeof(ins); // ++ so that each time return diff address
    ULONG range = min(module_limit_ - addr, MAX_REL_ADDR);
    ULONG offset = SearchPattern(addr, range, ins, sizeof(ins));
    if (!offset) {
      //ITH_MSG(L"failed");
      ConsoleOutput("vnreng:System43: pattern not found");
      return false;
    }

    addr += offset + hook_offset;
  } while(0xe8 != *(BYTE *)addr); // function call
  //ITH_GROWL_DWORD(addr);

  HookParam hp = {};
  hp.addr = addr;
  hp.off = 4;
  hp.split = -0x18;
  hp.type = NO_CONTEXT|USING_SPLIT|USING_STRING;
  ConsoleOutput("vnreng: INSERT System43");
  NewHook(hp, L"System43");
  return false;
}

/********************************************************************************************
AtelierKaguya hook:
  Game folder contains message.dat. Used by AtelierKaguya games.
  Usually has font caching issue with TextOutA.
  Game engine uses EBP to set up stack frame so we can easily trace back.
  Keep step out until it's in main game module. We notice that either register or
  stack contains string pointer before call instruction. But it's not quite stable.
  In-depth analysis of the called function indicates that there's a loop traverses
  the string one character by one. We can set a hook there.
  This search process is too complex so I just make use of some characteristic
  instruction(add esi,0x40) to locate the right point.
********************************************************************************************/
bool InsertAtelierHook()
{
  //SafeFillRange(process_name_, &base, &size);
  //size=size-base;
  DWORD sig = 0x40c683; //add esi,0x40
  //i=module_base_+SearchPattern(module_base_,module_limit_-module_base_,&sig,3);
  DWORD i;
  for (i = module_base_; i < module_limit_ - 4; i++) {
    sig = *(DWORD *)i & 0xffffff;
    if (0x40c683 == sig)
      break;
  }
  if (i < module_limit_ - 4)
    for (DWORD j=i-0x200; i>j; i--)
      if (*(DWORD *)i == 0xff6acccc) { //find the function entry
        HookParam hp = {};
        hp.addr = i+2;
        hp.off = 8;
        hp.split = -0x18;
        hp.length_offset = 1;
        hp.type = USING_SPLIT;
        ConsoleOutput("vnreng: INSERT Aterlier KAGUYA");
        NewHook(hp, L"Atelier KAGUYA");
        //RegisterEngineType(ENGINE_ATELIER);
        return true;
      }

  ConsoleOutput("vnreng:Aterlier: failed");
  return false;
  //ConsoleOutput("Unknown Atelier KAGUYA engine.");
}
/********************************************************************************************
CIRCUS hook:
  Game folder contains advdata folder. Used by CIRCUS games.
  Usually has font caching issues. But trace back from GetGlyphOutline gives a hook
  which generate repetition.
  If we study circus engine follow Freaka's video, we can easily discover that
  in the game main module there is a static buffer, which is filled by new text before
  it's drawing to screen. By setting a hardware breakpoint there we can locate the
  function filling the buffer. But we don't have to set hardware breakpoint to search
  the hook address if we know some characteristic instruction(cmp al,0x24) around there.
********************************************************************************************/
bool InsertCircusHook1() // jichi 10/2/2013: Change return type to bool
{
  for (DWORD i = module_base_ + 0x1000; i < module_limit_ - 4; i++)
    if (*(WORD *)i==0xA3C)  //cmp al, 0xA; je
      for (DWORD j = i; j < i + 0x100; j++) {
        BYTE c = *(BYTE *)j;
        if (c == 0xC3)
          break;
        if (c == 0xe8) {
          DWORD k = *(DWORD *)(j+1)+j+5;
          if (k > module_base_ && k < module_limit_) {
            HookParam hp = {};
            hp.addr = k;
            hp.off = 0xc;
            hp.split = -0x18;
            hp.length_offset = 1;
            hp.type = DATA_INDIRECT|USING_SPLIT;
            ConsoleOutput("vnreng: INSERT CIRCUS#1");
            NewHook(hp, L"CIRCUS");
            //RegisterEngineType(ENGINE_CIRCUS);
            return true;
          }
        }
      }
      //break;
  //ConsoleOutput("Unknown CIRCUS engine");
  ConsoleOutput("vnreng:CIRCUS: failed");
  return false;
}

bool InsertCircusHook2() // jichi 10/2/2013: Change return type to bool
{
  for (DWORD i = module_base_ + 0x1000; i < module_limit_ -4; i++)
    if ((*(DWORD *)i & 0xffffff) == 0x75243c) { // cmp al, 24; je
      if (DWORD j = SafeFindEntryAligned(i, 0x80)) {
        HookParam hp = {};
        hp.addr = j;
        hp.off = 0x8;
        hp.type = USING_STRING;
        ConsoleOutput("vnreng: INSERT CIRCUS#2");
        NewHook(hp, L"CIRCUS");
        //RegisterEngineType(ENGINE_CIRCUS);
        return true;
      }
      break;
    }
  //ConsoleOutput("Unknown CIRCUS engine.");
  ConsoleOutput("vnreng:CIRCUS: failed");
  return false;
}

/********************************************************************************************
ShinaRio hook:
  Game folder contains rio.ini.
  Problem of default hook GetTextExtentPoint32A is that the text repeat one time.
  But KF just can't resolve the issue. ShinaRio engine always perform integrity check.
  So it's very difficult to insert a hook into the game module. Freaka suggests to refine
  the default hook by adding split parameter on the stack. So far there is 2 different
  version of ShinaRio engine that needs different split parameter. Seems this value is
  fixed to the last stack frame. We just navigate to the entry. There should be a
  sub esp,* instruction. This value plus 4 is just the offset we need.

  New ShinaRio engine (>=2.48) uses different approach.
********************************************************************************************/
static void SpecialHookShina(DWORD esp_base, HookParam* hp, DWORD* data, DWORD* split, DWORD* len)
{
  DWORD ptr = *(DWORD*)(esp_base-0x20);
  *split = ptr;
  char* str = *(char**)(ptr+0x160);
  strcpy(text_buffer, str);
  int skip = 0;
  for (str = text_buffer; *str; str++)
    if (str[0] == 0x5f) {
      if (str[1] == 0x72)
        str[0] = str[1]=1;
      else if (str[1] == 0x74) {
        while (str[0] != 0x2f)
          *str++ = 1;
        *str=1;
      }
    }

  for (str = text_buffer; str[skip];)
    if (str[skip] == 1)
      skip++;
    else {
      str[0]=str[skip];
      str++;
    }

  str[0] = 0;
  if (strcmp(text_buffer, text_buffer_prev) == 0)
    *len=0;
  else {
    for (skip = 0; text_buffer[skip]; skip++)
      text_buffer_prev[skip]=text_buffer[skip];
    text_buffer_prev[skip] = 0;
    *data = (DWORD)text_buffer_prev;
    *len = skip;
  }
}

// jichi 8/27/2013
// Return ShinaRio version number
// The head of Rio.ini usually looks like:
//     [椎名里緒 v2.49]
// This function will return 49 in the above case.
//
// Games from アトリエさくら do not have Rio.ini, but $procname.ini.
int GetShinaRioVersion()
{
  int ret = 0;
  HANDLE hFile = IthCreateFile(L"RIO.INI", FILE_READ_DATA, FILE_SHARE_READ, FILE_OPEN);
  if (hFile == INVALID_HANDLE_VALUE)  {
    size_t len = wcslen(process_name_);
    if (len > 3) {
      wchar_t fname[MAX_PATH];
      wcscpy(fname, process_name_);
      fname[len -1] = 'i';
      fname[len -2] = 'n';
      fname[len -3] = 'i';
      hFile = IthCreateFile(fname, FILE_READ_DATA, FILE_SHARE_READ, FILE_OPEN);
    }
  }

  if (hFile != INVALID_HANDLE_VALUE)  {
    IO_STATUS_BLOCK ios;
    //char *buffer,*version;//,*ptr;
    enum { BufferSize = 0x40 };
    char buffer[BufferSize];
    NtReadFile(hFile, 0, 0, 0, &ios, buffer, BufferSize, 0, 0);
    NtClose(hFile);
    if (buffer[0] == '[') {
      buffer[0x3f] = 0; // jichi 8/24/2013: prevent strstr from overflow
      if (char *version = strstr(buffer, "v2."))
        sscanf(version + 3, "%d", &ret); // +3 to skip "v2."
    }
  }
  return ret;
}

// jichi 8/24/2013: Rewrite ShinaRio logic.
bool InsertShinaHook()
{
  int ver = GetShinaRioVersion();
  if (ver >= 48) { // v2.48, v2.49
    HookParam hp = {};
    hp.addr = (DWORD)GetTextExtentPoint32A;
    hp.extern_fun = SpecialHookShina;
    hp.type = EXTERN_HOOK|USING_STRING;
    ConsoleOutput("vnreng: INSERT ShinaRio > 2.47");
    NewHook(hp, L"ShinaRio");
    //RegisterEngineType(ENGINE_SHINA);
    return true;

  } else if (ver > 40) // <= v2.47. Older games like あやかしびと does not require hcode
    if (DWORD s = Util::FindCallAndEntryBoth(
          (DWORD)GetTextExtentPoint32A,
          module_limit_ - module_base_,
          (DWORD)module_base_,
          0xec81)) {
      HookParam hp = {};
      hp.addr = (DWORD)GetTextExtentPoint32A;
      hp.off = 0x8;
      hp.split = *(DWORD*)(s + 2) + 4;
      hp.length_offset = 1;
      hp.type = DATA_INDIRECT|USING_SPLIT;
      ConsoleOutput("vnreng: INSERT ShinaRio <= 2.47");
      NewHook(hp, L"ShinaRio");
     //RegisterEngineType(ENGINE_SHINA);
    }
  ConsoleOutput("vnreng:ShinaRio: unknown version");
  return false;
}

bool InsertWaffleDynamicHook(LPVOID addr, DWORD frame, DWORD stack)
{
  if (addr != GetTextExtentPoint32A)
    return false;

  DWORD handler;
  __asm
  {
    mov eax,fs:[0]
    mov eax,[eax]
    mov eax,[eax]
    mov eax,[eax]
    mov eax,[eax]
    mov eax,[eax]
    mov ecx, [eax + 4]
    mov handler, ecx
  }

  union {
    DWORD i;
    BYTE *ib;
    DWORD *id;
  };
  // jichi 9/30/2013: Fix the bug in ITH logic where j is unintialized
  for (i = module_base_ + 0x1000; i < module_limit_ - 4; i++)
    if (*id == handler && *(ib - 1) == 0x68)
      if (DWORD t = SafeFindEntryAligned(i, 0x40)) {
        HookParam hp = {};
        hp.addr = t;
        hp.off = 8;
        hp.ind = 4;
        hp.length_offset = 1;
        hp.type = DATA_INDIRECT;
        ConsoleOutput("vnreng: INSERT Dynamic Waffle");
        NewHook(hp, L"Waffle");
        return true;
      }
  ConsoleOutput("vnreng:DynamicWaffle: failed");
  //ConsoleOutput("Unknown waffle engine.");
  return true; // jichi 12/25/2013: return true
}
//  DWORD retn,limit,str;
//  WORD ch;
//  NTSTATUS status;
//  MEMORY_BASIC_INFORMATION info;
//  str = *(DWORD*)(stack+0xC);
//  ch = *(WORD*)str;
//  if (ch<0x100) return false;
//  limit = (stack | 0xFFFF) + 1;
//  __asm int 3
//  for (stack += 0x10; stack < limit; stack += 4)
//  {
//    str = *(DWORD*)stack;
//    if ((str >> 16) != (stack >> 16))
//    {
//      status = NtQueryVirtualMemory(NtCurrentProcess(),(PVOID)str,MemoryBasicInformation,&info,sizeof(info),0);
//      if (!NT_SUCCESS(status) || info.Protect & PAGE_NOACCESS) continue; //Accessible
//    }
//    if (*(WORD*)(str + 4) == ch) break;
//  }
//  if (stack < limit)
//  {
//    for (limit = stack + 0x100; stack < limit ; stack += 4)
//    if (*(DWORD*)stack == -1)
//    {
//      retn = *(DWORD*)(stack + 4);
//      if (retn > module_base_ && retn < module_limit_)
//      {
//        HookParam hp = {};
//        hp.addr = retn + *(DWORD*)(retn - 4);
//        hp.length_offset = 1;
//        hp.off = -0x20;
//        hp.ind = 4;
//        //hp.split = 0x1E8;
//        hp.type = DATA_INDIRECT;
//        NewHook(hp, L"WAFFLE");
//        //RegisterEngineType(ENGINE_WAFFLE);
//        return true;
//      }
//
//    }
//
//  }

void InsertWaffleHook()
{
  for (DWORD i = module_base_ + 0x1000; i < module_limit_ - 4; i++)
    if (*(DWORD *)i == 0xac68) {
      HookParam hp = {};
      hp.addr = i;
      hp.length_offset = 1;
      hp.off = -0x20;
      hp.ind = 4;
      hp.split = 0x1e8;
      hp.type = DATA_INDIRECT|USING_SPLIT;
      ConsoleOutput("vnreng: INSERT WAFFLE");
      NewHook(hp, L"WAFFLE");
      return;
    }
  //ConsoleOutput("Probably Waffle. Wait for text.");
  trigger_fun_ = InsertWaffleDynamicHook;
  SwitchTrigger(true);
  ConsoleOutput("vnreng:WAFFLE: failed");
}

void InsertTinkerBellHook()
{
  //DWORD s1,s2,i;
  //DWORD ch=0x8141;
  DWORD i;
  WORD count;
  count = 0;
  HookParam hp = {};
  hp.length_offset = 1;
  hp.type = BIG_ENDIAN | NO_CONTEXT;
  for (i = module_base_; i< module_limit_ - 4; i++) {
    if (*(DWORD*)i == 0x8141) {
      BYTE t = *(BYTE*)(i - 1);
      if (t == 0x3d || t == 0x2d) {
        hp.off = -0x8;
        hp.addr = i - 1;
      } else if (*(BYTE*)(i-2) == 0x81) {
        t &= 0xF8;
        if (t == 0xF8 || t == 0xE8) {
          hp.off = -8 - ((*(BYTE*)(i-1) & 7) << 2);
          hp.addr = i - 2;
        }
      }
      if (hp.addr) {
        WCHAR hook_name[0x20];
        memcpy(hook_name, L"TinkerBell", 0x14);
        hook_name[0xA] = L'0' + count;
        hook_name[0xB] = 0;
        ConsoleOutput("vnreng:INSERT TinkerBell");
        NewHook(hp, hook_name);
        count++;
        hp.addr = 0;
      }
    }
  }
  ConsoleOutput("vnreng:TinkerBell: failed");
}

//  s1=SearchPattern(module_base_,module_limit_-module_base_-4,&ch,4);
//  if (s1)
//  {
//    for (i=s1;i>s1-0x400;i--)
//    {
//      if (*(WORD*)(module_base_+i)==0xec83)
//      {
//        hp.addr=module_base_+i;
//        NewHook(hp, L"C.System");
//        break;
//      }
//    }
//  }
//  s2=s1+SearchPattern(module_base_+s1+4,module_limit_-s1-8,&ch,4);
//  if (s2)
//  {
//    for (i=s2;i>s2-0x400;i--)
//    {
//      if (*(WORD*)(module_base_+i)==0xec83)
//      {
//        hp.addr=module_base_+i;
//        NewHook(hp, L"TinkerBell");
//        break;
//      }
//    }
//  }
//  //if (count)
  //RegisterEngineType(ENGINE_TINKER);

// jichi 3/19/2014: Insert both hooks
void InsertLuneHook()
{
  if (DWORD c = Util::FindCallOrJmpAbs((DWORD)ExtTextOutA, module_limit_ - module_base_, (DWORD)module_base_, true))
    if (DWORD addr = Util::FindCallAndEntryRel(c, module_limit_ - module_base_, (DWORD)module_base_, 0xec8b55)) {
      HookParam hp = {};
      hp.addr = addr;
      hp.off = 4;
      hp.type = USING_STRING;
      ConsoleOutput("vnreng:INSERT MBL-Furigana");
      NewHook(hp, L"MBL-Furigana");
    }
  if (DWORD c = Util::FindCallOrJmpAbs((DWORD)GetGlyphOutlineA, module_limit_ - module_base_, (DWORD)module_base_, true))
    if (DWORD addr = Util::FindCallAndEntryRel(c, module_limit_ - module_base_, (DWORD)module_base_, 0xec8b55)) {
      HookParam hp = {};
      hp.addr = addr;
      hp.off = 4;
      hp.split = -0x18;
      hp.length_offset = 1;
      hp.type = BIG_ENDIAN|USING_SPLIT;
      ConsoleOutput("vnreng:INSERT MBL");
      NewHook(hp, L"MBL");
    }
}
/********************************************************************************************
YU-RIS hook:
  Becomes common recently. I first encounter this game in Whirlpool games.
  Problem is name is repeated multiple times.
  Step out of function call to TextOuA, just before call to this function,
  there should be a piece of code to calculate the length of the name.
  This length is 2 for single character name and text,
  For a usual name this value is greater than 2.
********************************************************************************************/

bool InsertWhirlpoolHook()
{
  DWORD i,t;
  //IthBreak();
  DWORD entry = Util::FindCallAndEntryBoth((DWORD)TextOutA, module_limit_ - module_base_, module_base_, 0xec83);
  if (!entry) {
    ConsoleOutput("vnreng:YU-RIS: function entry does not exist");
    return false;
  }
  entry = Util::FindCallAndEntryRel(entry - 4, module_limit_ - module_base_, module_base_, 0xec83);
  if (!entry) {
    ConsoleOutput("vnreng:YU-RIS: function entry does not exist");
    return false;
  }
  entry = Util::FindCallOrJmpRel(entry-4,module_limit_-module_base_-0x10000,module_base_+0x10000,false);
  for (i = entry - 4; i > entry - 0x100; i--)
    if (*(WORD *)i==0xC085) {
      t = *(WORD *)(i+2);
      if ((t&0xff) == 0x76) {
        t = 4;
        break;
      }
      if ((t&0xffff) == 0x860f) {
        t = 8;
        break;
      }
    }
  if (i == entry - 0x100) {
    ConsoleOutput("vnreng:YU-RIS: pattern not exist");
    return false;
  }
  HookParam hp = {};
  hp.addr = i+t;
  hp.off = -0x24;
  hp.split = -0x8;
  hp.type = USING_STRING|USING_SPLIT;
  ConsoleOutput("vnreng: INSERT YU-RIS");
  NewHook(hp, L"YU-RIS");
  //RegisterEngineType(ENGINE_WHIRLPOOL);
  return true;
}

bool InsertCotophaHook()
{
  DWORD addr = Util::FindCallAndEntryAbs((DWORD)GetTextMetricsA,module_limit_-module_base_,module_base_,0xec8b55);
  if (!addr) {
    ConsoleOutput("vnreng:Cotopha: pattern not exist");
    return false;
  }
  HookParam hp = {};
  hp.addr = addr;
  hp.off = 4;
  hp.split = -0x1c;
  hp.type = USING_UNICODE|USING_SPLIT|USING_STRING;
  ConsoleOutput("vnreng: INSERT Cotopha");
  NewHook(hp, L"Cotopha");
  //RegisterEngineType(ENGINE_COTOPHA);
  return true;
}

bool InsertCatSystem2Hook()
{
  //DWORD search=0x95EB60F;
  //DWORD j,i=SearchPattern(module_base_,module_limit_-module_base_,&search,4);
  //if (i==0) return;
  //i+=module_base_;
  //for (j=i-0x100;i>j;i--)
  //  if (*(DWORD*)i==0xCCCCCCCC) break;
  //if (i==j) return;
  //hp.addr=i+4;
  //hp.off=-0x8;
  //hp.ind=4;
  //hp.split=4;
  //hp.split_ind=0x18;
  //hp.type=BIG_ENDIAN|DATA_INDIRECT|USING_SPLIT|SPLIT_INDIRECT;
  //hp.length_offset=1;

  DWORD addr = Util::FindCallAndEntryAbs((DWORD)GetTextMetricsA, module_limit_ - module_base_, module_base_, 0xff6acccc);
  if (addr) {
    ConsoleOutput("vnreng:CatSystem2: pattern not exist");
    return false;
  }
  HookParam hp = {};
  hp.addr = addr + 2;
  hp.off = 8;
  hp.split = -0x10;
  hp.length_offset = 1;
  hp.type = BIG_ENDIAN|USING_SPLIT;
  ConsoleOutput("vnreng: INSERT CatSystem2");
  NewHook(hp, L"CatSystem2");
  //RegisterEngineType(ENGINE_CATSYSTEM);
  return true;
}

bool InsertNitroPlusHook()
{
  // jichi 12/24/2013: The first byte could be changed
  BYTE ins[] = {0xb0, 0x74, 0x53};
  DWORD addr = SearchPattern(module_base_,module_limit_-module_base_,ins,3);
  if (!addr) {
    ConsoleOutput("vnreng:NitroPlus: pattern not exist");
    return false;
  }
  addr += module_base_;
  ins[0] = *(BYTE *)(addr+3)&3;
  while (*(WORD *)addr != 0xec83)
    addr--;
  HookParam hp = {};
  hp.addr = addr;
  hp.off = -0x14+ (ins[0] << 2);
  hp.length_offset = 1;
  hp.type |= BIG_ENDIAN;
  ConsoleOutput("vnreng: INSERT NitroPlus");
  NewHook(hp, L"NitroPlus");
  //RegisterEngineType(ENGINE_NITROPLUS);
  return true;
}

bool InsertRetouchHook()
{
  DWORD addr;
  if (GetFunctionAddr("?printSub@RetouchPrintManager@@AAE_NPBDAAVUxPrintData@@K@Z", &addr, nullptr, nullptr, nullptr) ||
      GetFunctionAddr("?printSub@RetouchPrintManager@@AAEXPBDKAAH1@Z", &addr, nullptr, nullptr, nullptr)) {
    HookParam hp = {};
    hp.addr = addr;
    hp.off = 4;
    hp.type = USING_STRING;
    ConsoleOutput("vnreng: INSERT RetouchSystem");
    NewHook(hp, L"RetouchSystem");
    return true;
  }
  ConsoleOutput("vnreng:RetouchSystem: failed");
  //ConsoleOutput("Unknown RetouchSystem engine.");
  return false;
}

namespace { // unnamed Malie
/********************************************************************************************
Malie hook:
  Process name is malie.exe.
  This is the most complicate code I have made. Malie engine store text string in
  linked list. We need to insert a hook to where it travels the list. At that point
  EBX should point to a structure. We can find character at -8 and font size at +10.
  Also need to enable ITH suppress function.
********************************************************************************************/
bool InsertMalieHook1()
{
  const DWORD sig1 = 0x05e3c1;
  enum { sig1_size = 3 };
  DWORD i = SearchPattern(module_base_, module_limit_ - module_base_, &sig1, sig1_size);
  if (!i) {
    ConsoleOutput("vnreng:MalieHook1: pattern i not exist");
    return false;
  }

  const WORD sig2 = 0xc383;
  enum { sig2_size = 2 };
  DWORD j = i + module_base_ + sig1_size;
  i = SearchPattern(j, module_limit_ - j, &sig2, sig2_size);
  //if (!j)
  if (!i) { // jichi 8/19/2013: Change the condition fro J to I
    ConsoleOutput("vnreng:MalieHook1: pattern j not exist");
    return false;
  }
  HookParam hp = {};
  hp.addr = j + i;
  hp.off = -0x14;
  hp.ind = -0x8;
  hp.split = -0x14;
  hp.split_ind = 0x10;
  hp.length_offset = 1;
  hp.type = USING_UNICODE|USING_SPLIT|DATA_INDIRECT|SPLIT_INDIRECT;
  ConsoleOutput("vnreng: INSERT MalieHook1");
  NewHook(hp, L"Malie");
  //RegisterEngineType(ENGINE_MALIE);
  return true;
}

DWORD malie_furi_flag_; // jichi 8/20/2013: Make it global so that it can be reset
void SpecialHookMalie(DWORD esp_base, HookParam *hp, DWORD *data, DWORD *split, DWORD *len)
{
  DWORD ch = *(DWORD *)(esp_base - 0x8) & 0xffff,
        ptr = *(DWORD *)(esp_base - 0x24);
  *data = ch;
  *len = 2;
  if (malie_furi_flag_) {
    DWORD index = *(DWORD *)(esp_base - 0x10);
    if (*(WORD *)(ptr + index * 2 - 2) < 0xa)
      malie_furi_flag_ = 0;
  }
  else if (ch == 0xa) {
    malie_furi_flag_ = 1;
    len = 0;
  }
  *split = malie_furi_flag_;
}

bool InsertMalieHook2() // jichi 8/20/2013: Change return type to boolean
{
  const BYTE ins[] = {0x66,0x3d,0x1,0x0};
  DWORD p = SearchPattern(module_base_, module_limit_ - module_base_, ins, 4);
  if (!p) {
    ConsoleOutput("vnreng:MalieHook2: pattern not exist");
    return false;
  }
  BYTE *ptr = (BYTE *)(p + module_base_);
  while (true) {
    if (*(WORD *)ptr == 0x3d66) {
      ptr += 4;
      if (ptr[0] == 0x75) {
        ptr += ptr[1]+2;
        continue;
      }
      if (*(WORD *)ptr == 0x850f) {
        ptr += *(DWORD *)(ptr + 2) + 6;
        continue;
      }
    }
    break;
  }
  malie_furi_flag_ = 0; // reset old malie flag
  HookParam hp = {};
  hp.addr = (DWORD)ptr + 4;
  hp.off = -8;
  hp.length_offset = 1;
  hp.extern_fun = SpecialHookMalie;
  hp.type = EXTERN_HOOK|USING_SPLIT|USING_UNICODE|NO_CONTEXT;
  ConsoleOutput("vnreng: INSERT MalieHook2");
  NewHook(hp, L"Malie");
  //RegisterEngineType(ENGINE_MALIE);
  return true;
}

/**
 *  jichi 12/17/2013: Added for Electro Arms
 *  Observations from Electro Arms:
 *  1. split = 0xC can handle most texts and its dwRetn is always zero
 *  2. The text containing furigana needed to split has non-zero dwRetn when split = 0
 */
void SpecialHookMalie2(DWORD esp_base, HookParam *hp, DWORD *data, DWORD *split, DWORD *len)
{
  static DWORD last_split; // FIXME: This makes the special function stateful
  DWORD s1 = *(DWORD *)esp_base; // current split at 0x0
  if (!s1)
    *split = last_split;
  else {
    DWORD s2 = *(DWORD *)(esp_base + 0xc); // second split
    *split = last_split = s1 + s2; // not sure if plus is a good way
  }
  //*len = GetHookDataLength(*hp, esp_base, (DWORD)data);
  *len = 2;
}

/**
 *  jichi 8/20/2013: Add hook for sweet light BRAVA!!
 *  See: http://www.hongfire.com/forum/printthread.php?t=36807&pp=10&page=680
 *
 *  BRAVA!! /H code: "/HWN-4:C@1A3DF4:malie.exe"
 *  - addr: 1719796 = 0x1a3df4
 *  - extern_fun: 0x0
 *  - function: 0
 *  - hook_len: 0
 *  - ind: 0
 *  - length_offset: 1
 *  - module: 751199171 = 0x2cc663c3
 *  - off: 4294967288 = 0xfffffff8L = -0x8
 *  - recover_len: 0
 *  - split: 12 = 0xc
 *  - split_ind: 0
 *  - type: 1106 = 0x452
 */
bool InsertMalie2Hook()
{
  // 001a3dee    6900 70000000   imul eax,dword ptr ds:[eax],70
  // 001a3df4    0200            add al,byte ptr ds:[eax]   ; this is the place to hook
  // 001a3df6    50              push eax
  // 001a3df7    0069 00         add byte ptr ds:[ecx],ch
  // 001a3dfa    0000            add byte ptr ds:[eax],al
  const BYTE ins1[] = {
    0x40,            // inc eax
    0x89,0x56, 0x08, // mov dword ptr ds:[esi+0x8],edx
    0x33,0xd2,       // xor edx,edx
    0x89,0x46, 0x04  // mov dword ptr ds:[esi+0x4],eax
  };
  ULONG range1 = min(module_limit_ - module_base_, MAX_REL_ADDR);
  ULONG p = SearchPattern(module_base_, range1, ins1, sizeof(ins1));
  //reladdr = 0x1a3df4;
  if (!p) {
    //ITH_MSG(0, "Wrong1", "t", 0);
    //ConsoleOutput("Not malie2 engine");
    ConsoleOutput("vnreng:Malie2Hook: pattern p not exist");
    return false;
  }

  p += sizeof(ins1); // skip ins1
  //const BYTE ins2[] = { 0x85, 0xc0 }; // test eax,eax
  WORD ins2 = 0xc085; // test eax,eax
  enum { range2 = 0x200 };
  enum { hook_offset = 0 };
  ULONG q = SearchPattern(module_base_ + p, range2, &ins2, sizeof(ins2));
  if (!q) {
    //ITH_MSG(0, "Wrong2", "t", 0);
    //ConsoleOutput("Not malie2 engine");
    ConsoleOutput("vnreng:Malie2Hook: pattern q not exist");
    return false;
  }

  ULONG reladdr = p + q;
  HookParam hp = {};
  hp.addr = module_base_ + reladdr + hook_offset;
  hp.off = -8;
  hp.length_offset = 1;
  //hp.split = 0xc; // jichi 12/17/2013: Subcontext removed
  //hp.split = -0xc; // jichi 12/17/2013: This could split the furigana, but will mess up the text
  //hp.type = USING_SPLIT|USING_UNICODE|NO_CONTEXT;
  // jichi 12/17/2013: Need extern func for Electro Arms
  // Though the hook parameter is quit similar to Malie, the original extern function does not work
  hp.type = EXTERN_HOOK|USING_SPLIT|USING_UNICODE|NO_CONTEXT;
  hp.extern_fun = SpecialHookMalie2;
  ConsoleOutput("vnreng: INSERT Malie2");
  NewHook(hp, L"Malie2");

  //ITH_GROWL_DWORD2(hp.addr, reladdr);
  //RegisterEngineType(ENGINE_MALIE);
  return true;
}

// jichi 2/8/3014: Return the beginning and the end of the text
// Remove the leading illegal characters
enum { _MALIE3_MAX_LENGTH = 1500 }; // slightly larger than VNR's text limit (1000)
LPCWSTR _Malie3LTrim(LPCWSTR p)
{
  if (p)
    for (int count = 0; count < _MALIE3_MAX_LENGTH; count++,
        p++)
      if (p[0] == L'v' && p[1] == L'_') { // ex. v_akr0001, v_mzk0001
        p += 9;
        return p; // must return otherwise trimming more will break the ITH repetition elimination
      } else if (p[0] > 9) // ltrim illegal characers
        return p;
  return nullptr;
}
// Remove the trailing illegal characters
LPCWSTR _Malie3RTrim(LPCWSTR p)
{
  if (p)
    for (int count = 0; count < _MALIE3_MAX_LENGTH; count++,
         p--)
      if (p[-1] > 9) {
        if (p[-1] >= L'0' && p[-1] <= L'9'&& p[-1-7] == L'_')
          p -= 9;
        else
          return p;
      }
  return nullptr;
}
// Return the end of the line
LPCWSTR _Malie3GetEOL(LPCWSTR p)
{
  if (p)
    for (int count = 0; count < _MALIE3_MAX_LENGTH; count++,
        p++)
      switch (p[0]) {
      case 0: // \0
      case 0xa: // \n // the text after 0xa is furigana
        return p;
      }
  return nullptr;
}

/**
 *  jichi 3/8/2014: Add hook for 相州戦神館學園 八命陣
 *  See: http://sakuradite.com/topic/157
 *  check 0x5b51ed for ecx+edx*2
 *  Also need to skip furigana.
 */

void SpecialHookMalie3(DWORD esp_base, HookParam *hp, DWORD *data, DWORD *split, DWORD *len)
{
  CC_UNUSED(split);
  DWORD ecx = *(DWORD *)(esp_base + pusha_ecx_off - 4),
        edx = *(DWORD *)(esp_base + pusha_edx_off - 4);
  //*data = ecx + edx*2; // [ecx+edx*2];
  //*len = wcslen((LPCWSTR)data) << 2;
  // There are garbage characters
  LPCWSTR start = _Malie3LTrim((LPCWSTR)(ecx + edx*2)),
          stop = _Malie3RTrim(_Malie3GetEOL(start));
  *data = (DWORD)start;
  *len = max(0, stop - start) << 1;
  *split = 0x10001; // fuse all threads, and prevent floating
  //ITH_GROWL_DWORD5((DWORD)start, (DWORD)stop, *len, (DWORD)*start, (DWORD)_Malie3GetEOL(start));
}

/**
 *  jichi 8/20/2013: Add hook for 相州戦神館學園 八命陣
 *  See: http://sakuradite.com/topic/157
 *  Credits: @ok123
 */
bool InsertMalie3Hook()
{
  const BYTE ins[] = {
    // 0x90 nop
    0x8b,0x44,0x24, 0x04,   // 5b51e0  mov eax,dword ptr ss:[esp+0x4]
    0x56,                   // 5b51e4  push esi
    0x57,                   // 5b51e5  push edi
    0x8b,0x50, 0x08,        // 5b51e6  mov edx,dword ptr ds:[eax+0x8]
    0x8b,0x08,              // 5b51e9  mov ecx,dword ptr ds:[eax]
    0x33,0xf6,              // 5b51eb  xor esi,esi
    0x66,0x8b,0x34,0x51,    // 5b51ed  mov si,word ptr ds:[ecx+edx*2] // jichi: hook here
    0x42                    // 5b51f1  inc edx
  };
  enum {hook_offset = 0x5b51ed - 0x5b51e0};
  DWORD reladdr = SearchPattern(module_base_, module_limit_ - module_base_, ins, sizeof(ins));
  if (!reladdr) {
    ConsoleOutput("vnreng:Malie3: pattern not found");
    return false;
  }
  HookParam hp = {};
  hp.addr = module_base_ + reladdr + hook_offset;
  //ITH_GROWL(hp.addr);
  //hp.addr = 0x5b51ed;
  //hp.addr = 0x5b51f1;
  //hp.addr = 0x5b51f2;
  hp.extern_fun = SpecialHookMalie3;
  hp.type = EXTERN_HOOK|USING_UNICODE|NO_CONTEXT;
  ConsoleOutput("vnreng: INSERT Malie3");
  NewHook(hp, L"Malie3");
  return true;
}

} // unnamed Malie

bool InsertMalieHook()
{
  if (IthCheckFile(L"tools.dll"))
    return InsertMalieHook1(); // For Dies irae, etc
  else {
    // jichi 8/20/2013: Add hook for sweet light engine
    // Insert both malie and malie2 hook.
    bool ok = InsertMalieHook2();
    ok = InsertMalie2Hook() || ok; // jichi 8/20/2013 TO BE RESTORED
    ok = InsertMalie3Hook() || ok; // jichi 3/7/2014
    return ok;
  }
}

/********************************************************************************************
EMEHook hook: (Contributed by Freaka)
  EmonEngine is used by LoveJuice company and TakeOut. Earlier builds were apparently
  called Runrun engine. String parsing varies a lot depending on the font settings and
  speed setting. E.g. without antialiasing (which very early versions did not have)
  uses TextOutA, fast speed triggers different functions then slow/normal. The user can
  set his own name and some odd control characters are used (0x09 for line break, 0x0D
  for paragraph end) which is parsed and put together on-the-fly while playing so script
  can't be read directly.
********************************************************************************************/
bool InsertEMEHook()
{
  DWORD addr = Util::FindCallOrJmpAbs((DWORD)IsDBCSLeadByte,module_limit_-module_base_,(DWORD)module_base_,false);
  // no needed as first call to IsDBCSLeadByte is correct, but sig could be used for further verification
  //WORD sig = 0x51C3;
  //while (c && (*(WORD*)(c-2)!=sig))
  //{
  //  //-0x1000 as FindCallOrJmpAbs always uses an offset of 0x1000
  //  c = Util::FindCallOrJmpAbs((DWORD)IsDBCSLeadByte,module_limit_-c-0x1000+4,c-0x1000+4,false);
  //}
  if (!addr) {
    ConsoleOutput("vnreng:EME: pattern does not exist");
    return false;
  }
  HookParam hp = {};
  hp.addr = addr;
  hp.off = -0x8;
  hp.length_offset = 1;
  hp.type = NO_CONTEXT|DATA_INDIRECT;
  ConsoleOutput("vnreng: INSERT EmonEngine");
  NewHook(hp, L"EmonEngine");
  //ConsoleOutput("EmonEngine, hook will only work with text speed set to slow or normal!");
  //else ConsoleOutput("Unknown EmonEngine engine");
  return true;
}
void SpecialRunrunEngine(DWORD esp_base, HookParam* hp, DWORD* data, DWORD* split, DWORD* len)
{
  DWORD p1 = *(DWORD*)(esp_base-0x8)+*(DWORD*)(esp_base-0x10); //eax+edx
  *data = *(WORD*)(p1);
  *len = 2;
}
bool InsertRREHook()
{
  DWORD addr = Util::FindCallOrJmpAbs((DWORD)IsDBCSLeadByte,module_limit_-module_base_,(DWORD)module_base_,false);
  if (!addr) {
    ConsoleOutput("vnreng:RRE: function call does not exist");
    return false;
  }
  WORD sig = 0x51c3;
  HookParam hp = {};
  hp.addr = addr;
  hp.length_offset = 1;
  hp.type = NO_CONTEXT|DATA_INDIRECT;
  if ((*(WORD *)(addr-2) != sig)) {
    hp.extern_fun = SpecialRunrunEngine;
    hp.type |= EXTERN_HOOK;
    ConsoleOutput("vnreng: INSERT Runrun#1");
    NewHook(hp, L"RunrunEngine Old");
  } else {
    hp.off = -0x8;
    ConsoleOutput("vnreng: INSERT Runrun#2");
    NewHook(hp, L"RunrunEngine");
  }
  return true;
  //ConsoleOutput("RunrunEngine, hook will only work with text speed set to slow or normal!");
  //else ConsoleOutput("Unknown RunrunEngine engine");
}
bool InsertMEDHook()
{
  for (DWORD i = module_base_; i < module_limit_ - 4; i++)
    if (*(DWORD *)i == 0x8175) //cmp *, 8175
      for (DWORD j = i, k = i + 0x100; j < k; j++)
        if (*(BYTE *)j == 0xe8) {
          DWORD t = j + 5 + *(DWORD*)(j+1);
          if (t > module_base_ && t < module_limit_) {
            HookParam hp = {};
            hp.addr = t;
            hp.off = -0x8;
            hp.length_offset = 1;
            hp.type = BIG_ENDIAN;
            ConsoleOutput("vnreng: INSERT MED");
            NewHook(hp, L"MED");
            //RegisterEngineType(ENGINE_MED);
            return true;
          }
        }

  //ConsoleOutput("Unknown MED engine.");
  ConsoleOutput("vnreng:MED: failed");
  return false;
}
/********************************************************************************************
AbelSoftware hook:
  The game folder usually is made up many no extended name files(file name doesn't have '.').
  And these files have common prefix which is the game name, and 2 digit in order.


********************************************************************************************/
bool InsertAbelHook()
{
  const DWORD character[] = {0xc981d48a, 0xffffff00};
  if (DWORD j = SearchPattern(module_base_,module_limit_-module_base_, character, sizeof(character))) {
    j += module_base_;
    for (DWORD i = j-0x100; j > i; j--)
      if (*(WORD *)j == 0xff6a) {
        HookParam hp = {};
        hp.addr = j;
        hp.off = 4;
        hp.type = USING_STRING|NO_CONTEXT;
        ConsoleOutput("vnreng: INSERT AbelSoftware");
        NewHook(hp, L"AbelSoftware");
        //RegisterEngineType(ENGINE_ABEL);
        return true;
      }
  }
  ConsoleOutput("vnreng:Abel: failed");
  return false;
}
bool InsertLiveDynamicHook(LPVOID addr, DWORD frame, DWORD stack)
{
  if (addr != GetGlyphOutlineA || !frame)
    return false;
  DWORD k = *(DWORD *)frame;
  k = *(DWORD *)(k+4);
  if (*(BYTE *)(k-5)!=0xe8)
    k = *(DWORD *)(frame + 4);
  DWORD j = k + *(DWORD *)(k - 4);
  if (j > module_base_ && j < module_limit_) {
    HookParam hp = {};
    hp.addr = j;
    hp.off = -0x10;
    hp.length_offset = 1;
    hp.type |= BIG_ENDIAN;
    ConsoleOutput("vnreng: INSERT DynamicLive");
    NewHook(hp, L"Live");
    //RegisterEngineType(ENGINE_LIVE);
    return true;
  }
  ConsoleOutput("vnreng:DynamicLive: failed");
  return true; // jichi 12/25/2013: return true
}
//void InsertLiveHook()
//{
//  ConsoleOutput("Probably Live. Wait for text.");
//  trigger_fun_=InsertLiveDynamicHook;
//  SwitchTrigger(true);
//}
bool InsertLiveHook()
{
  const BYTE ins[] = {0x64,0x89,0x20,0x8b,0x45,0x0c,0x50};
  DWORD reladdr = SearchPattern(module_base_, module_limit_ - module_base_, ins, sizeof(ins));
  if (!reladdr) {
    ConsoleOutput("vnreng:Live: pattern not found");
    return false;
  }
  HookParam hp = {};
  hp.addr = reladdr + module_base_;
  hp.off = -0x10;
  hp.length_offset = 1;
  hp.type |= BIG_ENDIAN;
  ConsoleOutput("vnreng: INSERT Live");
  NewHook(hp, L"Live");
  //RegisterEngineType(ENGINE_LIVE);
  //else ConsoleOutput("Unknown Live engine");
  return true;
}

void InsertBrunsHook()
{
  if (IthCheckFile(L"libscr.dll")) {
    HookParam hp = {};
    hp.off = 4;
    hp.length_offset = 1;
    hp.type = USING_UNICODE|MODULE_OFFSET|FUNCTION_OFFSET;
    // jichi 12/27/2013: This function does not work for the latest bruns games anymore
    hp.function = 0x8b24c7bc;
    //?push_back@?$basic_string@GU?$char_traits@G@std@@V?$allocator@G@2@@std@@QAEXG@Z
    if (IthCheckFile(L"msvcp90.dll"))
      hp.module = 0xc9c36a5b; // 3385027163
    else if (IthCheckFile(L"msvcp80.dll"))
      hp.module = 0xa9c36a5b; // 2848156251
    else if (IthCheckFile(L"msvcp100.dll")) // jichi 8/17/2013: MSVCRT 10.0 and 11.0
      hp.module = 0xb571d760; // 3044136800;
    else if (IthCheckFile(L"msvcp110.dll"))
      hp.module = 0xd571d760; // 3581007712;
    if (hp.module) {
      ConsoleOutput("vnreng: INSERT Brus#1");
      NewHook(hp, L"Bruns");
    }
  }
  //else
  // jichi 12/21/2013: Keep both bruns hooks
  // The first one does not work for games like 「オーク・キングダム～モン娘繁殖の豚人王～」 anymore.
  {
    union {
      DWORD i;
      DWORD *id;
      WORD *iw;
      BYTE *ib;
    };
    DWORD k = module_limit_ - 4;
    for (i = module_base_ + 0x1000; i < k; i++) {
      if (*id != 0xff) //cmp reg,0xff
        continue;
      i += 4;
      if (*iw != 0x8f0f)
        continue;//jg
      i += 2;
      i += *id + 4;
      for (DWORD j = i + 0x40; i < j; i++) {
        if (*ib != 0xe8)
          continue;
        i++;
        DWORD t = i + 4 + *id;
        if (t > module_base_ && t <module_limit_) {
          i = t;
          for (j = i + 0x80; i < j; i++) {
            if (*ib != 0xe8)
              continue;
            i++;
            t = i + 4 + *id;
            if (t > module_base_ && t <module_limit_) {

              HookParam hp = {};
              hp.addr = t;
              hp.off = 4;
              hp.length_offset = 1;
              hp.type = USING_UNICODE|DATA_INDIRECT;
              ConsoleOutput("vnreng: INSERT Brus#2");
              NewHook(hp, L"Bruns2");
              return;
            }
          }
          k = i; //Terminate outer loop.
          break; //Terminate inner loop.
        }
      }
    }
  }
  //ConsoleOutput("Unknown Bruns engine.");
  ConsoleOutput("vnreng:Brus: failed");
}

/**
 * jichi 8/18/2013:  QLIE identified by GameData/data0.pack
 *
 * The old hook cannot recognize new games.
 */

namespace { // unnamed QLIE
/**
 * jichi 8/18/2013: new QLIE hook
 * See: http://www.hongfire.com/forum/showthread.php/420362-QLIE-engine-Hcode
 *
 * Ins:
 * 55 8B EC 53 8B 5D 1C
 * - 55         push ebp    ; hook here
 * - 8bec       mov ebp, esp
 * - 53         push ebx
 * - 8B5d 1c    mov ebx, dword ptr ss:[ebp+1c]
 *
 * /HBN14*0@4CC2C4
 * - addr: 5030596  (0x4cc2c4)
 * - extern_fun: 0x0
 * - function: 0
 * - hook_len: 0
 * - ind: 0
 * - length_offset: 1
 * - module: 0
 * - off: 20    (0x14)
 * - recover_len: 0
 * - split: 0
 * - split_ind: 0
 * - type: 1032 (0x408)
 */
bool InsertQLIE2Hook()
{
  const BYTE ins[] = { // size = 7
    0x55,           // 55       push ebp    ; hook here
    0x8b,0xec,      // 8bec     mov ebp, esp
    0x53,           // 53       push ebx
    0x8b,0x5d, 0x1c // 8b5d 1c  mov ebx, dword ptr ss:[ebp+1c]
  };
  enum { hook_offset = 0 }; // current instruction is the first one
  ULONG range = min(module_limit_ - module_base_, MAX_REL_ADDR);
  ULONG reladdr = SearchPattern(module_base_, range, ins, sizeof(ins));
  if (!reladdr) {
    ConsoleOutput("vnreng:QLIE2: pattern not found");
    //ConsoleOutput("Not QLIE2");
    return false;
  }

  HookParam hp = {};
  hp.type = DATA_INDIRECT | NO_CONTEXT; // 0x408
  hp.length_offset = 1;
  hp.off = 0x14;
  hp.addr = module_base_ + reladdr + hook_offset; //sizeof(ins) - cur_ins_size;

  ConsoleOutput("vnreng: INSERT QLIE2");
  NewHook(hp, L"QLIE2");
  //ConsoleOutput("QLIE2");
  return true;
}

// jichi: 8/18/2013: Change return type to bool
bool InsertQLIE1Hook()
{
  for (DWORD i = module_base_ + 0x1000; i < module_limit_ - 4; i++)
    if (*(DWORD *)i == 0x7ffe8347) { //inc edi, cmp esi,7f
      DWORD t = 0;
      for (DWORD j = i; j < i + 0x10; j++) {
        if (*(DWORD *)j == 0xa0) { //cmp esi,a0
          t = 1;
          break;
        }
      }
      if (t)
        for (DWORD j = i; j > i - 0x100; j--)
          if (*(DWORD *)j == 0x83ec8b55) { //push ebp, mov ebp,esp, sub esp,*
            HookParam hp = {};
            hp.addr = j;
            hp.off = 0x18;
            hp.split = -0x18;
            hp.length_offset = 1;
            hp.type = DATA_INDIRECT | USING_SPLIT;
            ConsoleOutput("vnreng: INSERT QLIE1");
            NewHook(hp, L"QLIE");
            //RegisterEngineType(ENGINE_FRONTWING);
            return true;
          }
    }

  ConsoleOutput("vnreng:QLIE1: failed");
  //ConsoleOutput("Unknown QLIE engine");
  return false;
}

} // unnamed QLIE

// jichi 8/18/2013: Add new hook
bool InsertQLIEHook()
{ return InsertQLIE1Hook() || InsertQLIE2Hook(); }

/********************************************************************************************
CandySoft hook:
  Game folder contains many *.fpk. Engine name is SystemC.
  I haven't seen this engine in other company/brand.

  AGTH /X3 will hook lstrlenA. One thread is the exactly result we want.
  But the function call is difficult to located programmatically.
  I find a equivalent points which is more easy to search.
  The script processing function needs to find 0x5B'[',
  so there should a instruction like cmp reg,5B
  Find this position and navigate to function entry.
  The first parameter is the string pointer.
  This approach works fine with game later than つよきす２学期.

  But the original つよきす is quite different. I handle this case separately.

********************************************************************************************/

namespace { // unnamed Candy

// jichi 8/23/2013: split into two different engines
//if (_wcsicmp(process_name_, L"systemc.exe")==0)
// Process name is "SystemC.exe"
bool InsertCandyHook1()
{
  for (DWORD i = module_base_ + 0x1000; i < module_limit_ - 4; i++)
    if ((*(DWORD *)i&0xffffff) == 0x24f980) // cmp cl,24
      for (DWORD j = i, k = i - 0x100; j > k; j--)
        if (*(DWORD *)j == 0xc0330a8a) { // mov cl,[edx]; xor eax,eax
          HookParam hp = {};
          hp.addr = j;
          hp.off = -0x10;
          hp.type = USING_STRING;
          ConsoleOutput("vnreng: INSERT SystemC#1");
          NewHook(hp, L"SystemC");
          //RegisterEngineType(ENGINE_CANDY);
          return true;
        }
  ConsoleOutput("vnreng:CandyHook1: failed");
  return false;
}

// jichi 8/23/2013: Process name is NOT "SystemC.exe"
bool InsertCandyHook2()
{
  for (DWORD i = module_base_ + 0x1000; i < module_limit_ - 4 ;i++)
    if (*(WORD *)i == 0x5b3c || // cmp al,0x5b
        (*(DWORD *)i & 0xfff8fc) == 0x5bf880) // cmp reg,0x5B
      for (DWORD j = i, k = i - 0x100; j > k; j--)
        if ((*(DWORD *)j & 0xffff) == 0x8b55) { // push ebp, mov ebp,esp, sub esp,*
          HookParam hp = {};
          hp.addr = j;
          hp.off = 4;
          hp.type = USING_STRING;
          ConsoleOutput("vnreng: INSERT SystemC#2");
          NewHook(hp, L"SystemC");
          //RegisterEngineType(ENGINE_CANDY);
          return true;
        }
  ConsoleOutput("vnreng:CandyHook2: failed");
  return false;
}

/** jichi 10/2/2013: CHECKPOINT
 *
 *  [5/31/2013] 恋もHもお勉強も、おまかせ！お姉ちゃん部
 *  base = 0xf20000
 *  + シナリオ: /HSN-4@104A48:ANEBU.EXE
 *    - off: 4294967288 = 0xfffffff8 = -8
 ,    - type: 1025 = 0x401
 *  + 選択肢: /HSN-4@104FDD:ANEBU.EXE
 *    - off: 4294967288 = 0xfffffff8 = -8
 *    - type: 1089 = 0x441
 */
//bool InsertCandyHook3()
//{
//  return false; // CHECKPOINT
//  const BYTE ins[] = {
//    0x83,0xc4, 0x0c, // add esp,0xc ; hook here
//    0x0f,0xb6,0xc0,  // movzx eax,al
//    0x85,0xc0,       // test eax,eax
//    0x75, 0x0e       // jnz XXOO ; it must be 0xe, or there will be duplication
//  };
//  enum { hook_offset = 0 };
//  ULONG range = min(module_limit_ - module_base_, MAX_REL_ADDR);
//  ULONG reladdr = SearchPattern(module_base_, range, ins, sizeof(ins));
//  reladdr = 0x104a48;
//  ITH_GROWL_DWORD(module_base_);
//  //ITH_GROWL_DWORD3(reladdr, module_base_, range);
//  if (!reladdr)
//    return false;
//
//  HookParam hp = {};
//  hp.addr = module_base_ + reladdr + hook_offset;
//  hp.off = -8;
//  hp.type = USING_STRING|NO_CONTEXT;
//  NewHook(hp, L"Candy");
//  return true;
//}

} // unnamed Candy

// jichi 10/2/2013: Add new candy hook
bool InsertCandyHook()
{
  if (0 == _wcsicmp(process_name_, L"systemc.exe"))
    return InsertCandyHook1();
  else
    return InsertCandyHook2();
    //bool b2 = InsertCandyHook2(),
    //     b3 = InsertCandyHook3();
    //return b2 || b3;
}

/********************************************************************************************
Apricot hook:
  Game folder contains arc.a*.
  This engine is heavily based on new DirectX interfaces.
  I can't find a good place where text is clean and not repeating.
  The game processes script encoded in UTF32-like format.
  I reversed the parsing algorithm of the game and implemented it partially.
  Only name and text data is needed.

********************************************************************************************/
static void SpecialHookApricot(DWORD esp_base, HookParam* hp, DWORD* data, DWORD* split, DWORD* len)
{
  DWORD reg_esi = *(DWORD *)(esp_base - 0x20);
  DWORD reg_esp = *(DWORD *)(esp_base - 0x18);
  DWORD base = *(DWORD *)(reg_esi + 0x24);
  DWORD index = *(DWORD *)(reg_esi + 0x3c);
  DWORD *script = (DWORD *)(base + index*4);
  *split = reg_esp;
  if (script[0] == L'<') {
    DWORD *end;
    for (end = script; *end != L'>'; end++);
    switch (script[1]) {
    case L'N':
      if (script[2] == L'a' && script[3] == L'm' && script[4] == L'e') {
        buffer_index = 0;
        for (script += 5; script < end; script++)
          if (*script > 0x20)
            wc_buffer[buffer_index++] = *script & 0xFFFF;
        *len = buffer_index<<1;
        *data = (DWORD)wc_buffer;
        // jichi 1/4/2014: The way I save subconext is not able to distinguish the split value
        // Change to shift 16
        //*split |= 1<<31;
        *split |= 1<<16;
      } break;
    case L'T':
      if (script[2] == L'e' && script[3] == L'x' && script[4] == L't') {
        buffer_index = 0;
        for (script += 5; script < end; script++) {
          if (*script > 0x40) {
            while (*script == L'{') {
              script++;
              while (*script!=L'\\') {
                wc_buffer[buffer_index++] = *script & 0xffff;
                script++;
              }
              while (*script++!=L'}');
            }
            wc_buffer[buffer_index++] = *script & 0xffff;
          }
        }
        *len = buffer_index << 1;
        *data = (DWORD)wc_buffer;
      } break;
    }
  }

}
bool InsertApricotHook()
{
  for (DWORD i = module_base_ + 0x1000; i < module_limit_ - 4; i++)
    if ((*(DWORD *)i & 0xfff8fc)==0x3cf880)  // cmp reg,0x3c
      for (DWORD j = i+3, k = i+0x100;j<k;j++)
        if ((*(DWORD *)j & 0xffffff) == 0x4c2) { // retn 4
          HookParam hp = {};
          hp.addr = j + 3;
          hp.extern_fun = SpecialHookApricot;
          hp.type = EXTERN_HOOK|USING_STRING|USING_UNICODE|NO_CONTEXT;
          ConsoleOutput("vnreng: INERT Apricot");
          NewHook(hp, L"ApRicot");
          //RegisterEngineType(ENGINE_APRICOT);
          return true;
        }

  ConsoleOutput("vnreng:Apricot: failed");
  return false;
}
void InsertStuffScriptHook()
{
  HookParam hp = {};
  hp.addr = (DWORD)GetTextExtentPoint32A;
  hp.off = 8;
  hp.split = -0x18;
  hp.type = USING_STRING | USING_SPLIT;
  ConsoleOutput("vnreng: INERT StuffScriptEngine");
  NewHook(hp, L"StuffScriptEngine");
  //RegisterEngine(ENGINE_STUFFSCRIPT);
}
bool InsertTriangleHook()
{
  for (DWORD i = module_base_; i < module_limit_ - 4; i++)
    if ((*(DWORD*)i & 0xffffff) == 0x75403c) // cmp al,0x40; jne
      for (DWORD j = i + 4 + *(BYTE*)(i+3), k = j + 0x20; j < k; j++)
        if (*(BYTE*)j == 0xe8) {
          DWORD t = j + 5 + *(DWORD *)(j + 1);
          if (t > module_base_ && t < module_limit_) {
            HookParam hp = {};
            hp.addr = t;
            hp.off = 4;
            hp.type = USING_STRING;
            ConsoleOutput("vnreng: INSERT Triangle");
            NewHook(hp, L"Triangle");
            //RegisterEngineType(ENGINE_TRIANGLE);
            return true;
          }
        }
  //ConsoleOutput("Old/Unknown Triangle engine.");
  ConsoleOutput("vnreng:Triangle: failed");
  return false;
}
bool InsertPensilHook()
{
  for (DWORD i = module_base_; i < module_limit_ - 4; i++)
    if (*(DWORD *)i == 0x6381) // cmp *,8163
      if (DWORD j = SafeFindEntryAligned(i, 0x100)) {
        HookParam hp = {};
        hp.addr = j;
        hp.off = 8;
        hp.length_offset = 1;
        ConsoleOutput("vnreng: INSERT Pensil");
        NewHook(hp, L"Pensil");
        return true;
        //RegisterEngineType(ENGINE_PENSIL);
      }
  //ConsoleOutput("Unknown Pensil engine.");
  ConsoleOutput("vnreng:Pensil: failed");
  return false;
}
bool IsPensilSetup()
{
  HANDLE hFile = IthCreateFile(L"PSetup.exe", FILE_READ_DATA, FILE_SHARE_READ, FILE_OPEN);
  FILE_STANDARD_INFORMATION info;
  IO_STATUS_BLOCK ios;
  LPVOID buffer = nullptr;
  NtQueryInformationFile(hFile, &ios, &info, sizeof(info), FileStandardInformation);
  NtAllocateVirtualMemory(NtCurrentProcess(), &buffer, 0,
      &info.AllocationSize.LowPart, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
  NtReadFile(hFile, 0,0,0, &ios, buffer, info.EndOfFile.LowPart, 0, 0);
  NtClose(hFile);
  BYTE *b = (BYTE *)buffer;
  DWORD len = info.EndOfFile.LowPart & ~1;
  if (len == info.AllocationSize.LowPart)
    len -= 2;
  b[len] = 0;
  b[len + 1] = 0;
  bool ret = wcsstr((LPWSTR)buffer, L"PENSIL") || wcsstr((LPWSTR)buffer, L"Pensil");
  NtFreeVirtualMemory(NtCurrentProcess(), &buffer, &info.AllocationSize.LowPart, MEM_RELEASE);
  return ret;
}
static void SpecialHookDebonosu(DWORD esp_base, HookParam* hp, DWORD* data, DWORD* split, DWORD* len)
{
  DWORD retn = *(DWORD*)esp_base;
  if (*(WORD*)retn == 0xc483) //add esp, *
    hp->off = 4;
  else
    hp->off = -0x8;
  hp->type ^= EXTERN_HOOK;
  hp->extern_fun = 0;
  *data = *(DWORD*)(esp_base + hp->off);
  *len = strlen((char*)*data);
}
bool InsertDebonosuHook()
{
  DWORD fun;
  if (CC_UNLIKELY(!GetFunctionAddr("lstrcatA", &fun, 0, 0, 0))) {
    ConsoleOutput("vnreng:Debonosu: failed to find lstrcatA");
    return false;
  }
  DWORD addr = Util::FindImportEntry(module_base_, fun);
  if (!addr) {
    ConsoleOutput("vnreng:Debonosu: lstrcatA is not called");
    return false;
  }
  DWORD search = 0x15ff | (addr << 16);
  addr >>= 16;
  for (DWORD i = module_base_; i < module_limit_ - 4; i++)
    if (*(DWORD *)i == search &&
        *(WORD *)(i + 4) == addr && // call dword ptr lstrcatA
        *(BYTE *)(i - 5) == 0x68) { // push $
      DWORD push = *(DWORD *)(i - 4);
      for (DWORD j = i + 6, k = j + 0x10; j < k; j++)
        if (*(BYTE *)j == 0xb8 &&
            *(DWORD *)(j + 1) == push)
          if (DWORD addr = SafeFindEntryAligned(i, 0x200)) {
            HookParam hp = {};
            hp.addr = addr;
            hp.extern_fun = SpecialHookDebonosu;
            hp.type = USING_STRING | EXTERN_HOOK;
            ConsoleOutput("vnreng: INSERT Debonosu");
            NewHook(hp, L"Debonosu");
            //RegisterEngineType(ENGINE_DEBONOSU);
            return true;
          }
      }

  ConsoleOutput("vnreng:Debonosu: failed");
  //ConsoleOutput("Unknown Debonosu engine.");
  return false;
}

static void SpecialHookSofthouse(DWORD esp_base, HookParam* hp, DWORD* data, DWORD* split, DWORD* len)
{
  DWORD i;
  union {
    LPWSTR string_u;
    PCHAR string_a;
  };
  string_u = *(LPWSTR *)(esp_base + 4);
  if (hp->type & USING_UNICODE) {
    *len = wcslen(string_u);
    for (i = 0; i < *len; i++)
      if (string_u[i] == L'>'||string_u[i] == L']') {
        *data = (DWORD)(string_u+i+1);
        *split = 0;
        *len -= i + 1;
        *len <<= 1;
        return;
      }
  } else {
    *len = strlen(string_a);
    for (i=0;i<*len;i++)
      if (string_a[i]=='>'||string_a[i]==']') {
        *data = (DWORD)(string_a+i+1);
        *split = 0;
        *len -= i+1;
        return;
      }

  }
}
bool InsertSofthouseDynamicHook(LPVOID addr, DWORD frame, DWORD stack)
{
  if (addr != DrawTextExA && addr != DrawTextExW)
    return false;
  DWORD high,low,i,j,k;
  Util::GetCodeRange(module_base_, &low, &high);
  i = stack;
  j = (i&0xffff0000)+0x10000;
  for (;i<j;i+=4) {
    k=*(DWORD*)i;
    if (k>low&&k<high) {
      if ((*(WORD*)(k-6)==0x15FF)||*(BYTE*)(k-5)==0xE8)
      {
        HookParam hp={};
        hp.off=0x4;
        hp.extern_fun=SpecialHookSofthouse;
        hp.type=USING_STRING|EXTERN_HOOK;
        if (addr==DrawTextExW) {hp.type|=USING_UNICODE;}
        i=*(DWORD*)(k-4);
        if (*(DWORD*)(k-5)==0xE8)
          hp.addr=i+k;
        else
          hp.addr=*(DWORD*)i;
        ConsoleOutput("vnreng: INSERT SofthouseChara");
        NewHook(hp, L"SofthouseChara");
        //RegisterEngineType(ENGINE_SOFTHOUSE);
        return true;
      }
    }
  }
  ConsoleOutput("vnreng:SofthouseChara: failed");
  return true; // jichi 12/25/2013: return true
}

void InsertSoftHouseHook()
{
  //ConsoleOutput("Probably SoftHouseChara. Wait for text.");
  trigger_fun_ = InsertSofthouseDynamicHook;
  SwitchTrigger(true);
}

static void SpecialHookCaramelBox(DWORD esp_base, HookParam* hp, DWORD* data, DWORD* split, DWORD* len)
{
  DWORD reg_ecx = *(DWORD*)(esp_base+hp->off);
  BYTE* ptr = (BYTE*)reg_ecx;
  buffer_index = 0;
  while (ptr[0])
  {
    if (ptr[0] == 0x28) // Furigana format: (Kanji,Furi)
    {
      ptr++;
      while (ptr[0]!=0x2C) //Copy Kanji
        text_buffer[buffer_index++] = *ptr++;
      while (ptr[0]!=0x29) // Skip Furi
        ptr++;
      ptr++;
    }
    else if (ptr[0] == 0x5C) ptr +=2;
    else
    {
      text_buffer[buffer_index++] = ptr[0];
      if (LeadByteTable[ptr[0]]==2)
      {
        ptr++;
        text_buffer[buffer_index++] = ptr[0];
      }
      ptr++;
    }
  }
  *len = buffer_index;
  *data = (DWORD)text_buffer;
  *split = 0;
}
// jichi 10/1/2013: Change return type to bool
bool InsertCaramelBoxHook()
{
  union { DWORD i; BYTE* pb; WORD* pw; DWORD* pd; };
  DWORD reg = -1;
  for (i = module_base_ + 0x1000; i < module_limit_ - 4; i++) {
    if (*pd == 0x7ff3d) //cmp eax, 7ff
      reg = 0;
    else if ((*pd & 0xfffff8fc) == 0x07fff880) //cmp reg, 7ff
      reg = pb[1] & 0x7;

    if (reg == -1)
      continue;

    DWORD flag = 0;
    if (*(pb - 6) == 3) { //add reg, [ebp+$disp_32]
      if (*(pb - 5) == (0x85 | (reg << 3)))
        flag = 1;
    } else if (*(pb - 3) == 3) { //add reg, [ebp+$disp_8]
      if (*(pb - 2) == (0x45 | (reg << 3)))
        flag = 1;
    } else if (*(pb - 2) == 3) { //add reg, reg
      if (((*(pb - 1) >> 3) & 7)== reg)
        flag = 1;
    }
    reg = -1;
    if (flag) {
      for (DWORD j = i, k = i - 0x100; j > k; j--) {
        if ((*(DWORD *)j&0xffff00ff) == 0x1000b8) { //mov eax,10??
          HookParam hp = {};
          hp.addr = j & ~0xF;
          hp.extern_fun = SpecialHookCaramelBox;
          hp.type = USING_STRING | EXTERN_HOOK;
          for (i &= ~0xFFFF; i < module_limit_ - 4; i++)
          {
            if (pb[0] == 0xE8)
            {
              pb++;
              if (pd[0] + i + 4 == hp.addr)
              {
                pb += 4;
                if ((pd[0] & 0xFFFFFF) == 0x04C483)
                  hp.off = 4;
                else hp.off = -0xC;
                break;
              }
            }
          }
          if (hp.off == 0) {
            ConsoleOutput("vnreng:CaramelBox: failed, zero off");
            return false;
          }
          ConsoleOutput("vnreng: INSERT CaramelBox");
          NewHook(hp, L"CaramelBox");
          //RegisterEngineType(ENGINE_CARAMEL);
          return true;
        }
      }
    }
  }
  ConsoleOutput("vnreng:CaramelBox: failed");
  return false;
//_unknown_engine:
  //ConsoleOutput("Unknown CarmelBox engine.");
}

/**
 *  jichi 10/12/2014
 *  P.S.: Another approach
 *  See: http://tieba.baidu.com/p/2425786155
 *  Quote:
 *  I guess this post should go in here. I got sick of AGTH throwing a fit when entering the menus in Wolf RPG games, so I did some debugging. This is tested and working properly with lots of games. If you find one that isn't covered then please PM me and I'll look into it.
 *
 *  Wolf RPG H-code - Use whichever closest matches your Game.exe
 *  /HBN*0@454C6C (2010/10/09 : 2,344KB : v1.31)
 *  /HBN*0@46BA03 (2011/11/22 : 2,700KB : v2.01)
 *  /HBN*0@470CEA (2012/05/07 : 3,020KB : v2.02)
 *  /HBN*0@470D5A (2012/06/10 : 3,020KB : v2.02a)
 *
 *  ith_p.cc:Ith::parseHookCode: enter: code = "/HBN*0@470CEA"
 *  - addr: 4656362 ,
 *  - length_offset: 1
 *  - type: 1032 = 0x408
 *
 *  Use /HB instead of /HBN if you want to split dialogue text and menu text into separate threads.
 *  Also set the repetition trace parameters in AGTH higher or it won't work properly with text-heavy menus. 64 x 16 seems to work fine.
 *
 *  Issues:
 *  AGTH still causes a bit of lag when translating menus if you have a lot of skills or items.
 *  Using ITH avoids this problem, but it sometimes has issues with repetition detection which can be fixed by quickly deselecting and reselecting the game window; Personally I find this preferable to menu and battle slowdown that AGTH sometimes causes, but then my PC is pretty slow so you might not have that problem.
 *
 *  Minimising the AGTH/ITH window generally makes the game run a bit smoother as windows doesn't need to keep scrolling the text box as new text is added.
 *
 *  RPG Maker VX H-code:
 *  Most games are detected automatically and if not then by using the AGTH /X or /X2 or /X3 parameters.
 *
 *  Games that use TRGSSX.dll may have issues with detection (especially with ITH).
 *  If TRGSSX.dll is included with the game then this code should work:
 *  /HQN@D3CF:TRGSSX.dll
 *
 *  With this code, using AGTH to start the process will not work. You must start the game normally and then hook the process afterwards.
 *  ITH has this functionality built into the interface. AGTH requires the /PN command line argument, for example:
 *  agth /PNGame.exe /HQN@D3CF:TRGSSX.dll /C
 *
 *  Again, drop the N to split dialogue and menu text into separate threads.
 */
// jichi 10/13/2013: restored
bool InsertWolfHook()
{
  //__asm int 3   // debug
  // jichi 10/12/2013:
  // Step 1: find the address of GetTextMetricsA
  // Step 2: find where this function is called
  // Step 3: search "sub esp, XX" after where it is called
  if (DWORD c1 = Util::FindCallAndEntryAbs((DWORD)GetTextMetricsA, module_limit_ - module_base_, module_base_, 0xec81))
    if (DWORD c2 = Util::FindCallOrJmpRel(c1, module_limit_ - module_base_, module_base_, 0)) {
      union {
        DWORD i;
        WORD *k;
      };
      DWORD j;
      for (i = c2 - 0x100, j = c2 - 0x400; i > j; i--)
        if (*k == 0xec83) { // jichi 10/12/2013: 83 EC XX   sub esp, XX  See: http://lists.cs.uiuc.edu/pipermail/llvm-commits/Week-of-Mon-20120312.txt
          HookParam hp = {};
          hp.addr = i;
          hp.off = -0xc;
          hp.split = -0x18;
          hp.type = DATA_INDIRECT | USING_SPLIT;
          hp.length_offset = 1;
          ConsoleOutput("vnreng: INSERT WolfRPG");
          NewHook(hp, L"WolfRPG");
          return true;
        }
    }

  //ConsoleOutput("Unknown WolfRPG engine.");
  ConsoleOutput("vnreng:WolfRPG: failed");
  return false;
}

bool InsertIGSDynamicHook(LPVOID addr, DWORD frame, DWORD stack)
{
  if (addr != GetGlyphOutlineW)
    return false;
  DWORD i;
  i = *(DWORD *)frame;
  i = *(DWORD *)(i+4);
  DWORD j, k;
  if (SafeFillRange(L"mscorlib.ni.dll", &j, &k)) {
    while (*(BYTE *)i != 0xe8)
      i++;
    DWORD t = *(DWORD *)(i + 1) + i + 5;
    if (t>j && t<k) {
      HookParam hp = {};
      hp.addr = t;
      hp.off = -0x10;
      hp.split = -0x18;
      hp.length_offset = 1;
      hp.type = USING_UNICODE|USING_SPLIT;
      ConsoleOutput("vnreng: INSERT IronGameSystem");
      NewHook(hp, L"IronGameSystem");
      //ConsoleOutput("IGS - Please set text(テキスト) display speed(表示速度) to fastest(瞬間)");
      //RegisterEngineType(ENGINE_IGS);
      return true;
    }
  }
  ConsoleOutput("vnreng:IGS: failed");
  return true; // jichi 12/25/2013: return true
}
void InsertIronGameSystemHook()
{
  //ConsoleOutput("Probably IronGameSystem. Wait for text.");
  trigger_fun_ = InsertIGSDynamicHook;
  SwitchTrigger(true);
  ConsoleOutput("vnreng: TRIGGER IronGameSystem");
}

/********************************************************************************************
AkabeiSoft2Try hook:
  Game folder contains YaneSDK.dll. Maybe we should call the engine Yane(屋根 = roof)?
  This engine is based on .NET framework. This really makes it troublesome to locate a
  valid hook address. The problem is that the engine file merely contains bytecode for
  the CLR. Real meaningful object code is generated dynamically and the address is randomized.
  Therefore the easiest method is to brute force search whole address space. While it's not necessary
  to completely search the whole address space, since non-executable pages can be excluded first.
  The generated code sections do not belong to any module(exe/dll), hence they do not have
  a section name. So we can also exclude executable pages from all modules. At last, the code
  section should be long(>0x2000). The remain address space should be several MBs in size and
  can be examined in reasonable time(less than 0.1s for P8400 Win7x64).
  Characteristic sequence is 0F B7 44 50 0C, stands for movzx eax, word ptr [edx*2 + eax + C].
  Obviously this instruction extracts one unicode character from a string.
  A main shortcoming is that the code is not generated if it hasn't been used yet.
  So if you are in title screen this approach will fail.

********************************************************************************************/
MEMORY_WORKING_SET_LIST *GetWorkingSet()
{
  DWORD len,retl;
  NTSTATUS status;
  LPVOID buffer = 0;
  len = 0x4000;
  status = NtAllocateVirtualMemory(NtCurrentProcess(), &buffer, 0, &len, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
  if (!NT_SUCCESS(status)) return 0;
  status = NtQueryVirtualMemory(NtCurrentProcess(), 0, MemoryWorkingSetList, buffer, len, &retl);
  if (status == STATUS_INFO_LENGTH_MISMATCH) {
    len = *(DWORD*)buffer;
    len = ((len << 2) & 0xFFFFF000) + 0x4000;
    retl = 0;
    NtFreeVirtualMemory(NtCurrentProcess(), &buffer, &retl, MEM_RELEASE);
    buffer = 0;
    status = NtAllocateVirtualMemory(NtCurrentProcess(), &buffer, 0, &len, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    if (!NT_SUCCESS(status)) return 0;
    status = NtQueryVirtualMemory(NtCurrentProcess(), 0, MemoryWorkingSetList, buffer, len, &retl);
    if (!NT_SUCCESS(status)) return 0;
    return (MEMORY_WORKING_SET_LIST*)buffer;
  } else {
    retl = 0;
    NtFreeVirtualMemory(NtCurrentProcess(), &buffer, &retl, MEM_RELEASE);
    return 0;
  }

}
typedef struct _NSTRING
{
  PVOID vfTable;
  DWORD lenWithNull;
  DWORD lenWithoutNull;
  WCHAR str[1];
} NSTRING;
static void SpecialHookAB2Try(DWORD esp_base, HookParam* hp, DWORD* data, DWORD* split, DWORD* len)
{
  DWORD test = *(DWORD*)(esp_base - 0x10);
  if (test != 0) return;
  NSTRING *s = *(NSTRING**)(esp_base - 0x8);
  *len = s->lenWithoutNull << 1;
  *data = (DWORD)s->str;
  *split = 0;
}

// qsort correctly identifies overflow.
int cmp(const void * a, const void * b)
{ return *(int*)a - *(int*)b; }

BOOL FindCharacteristInstruction(MEMORY_WORKING_SET_LIST* list)
{
  DWORD base,size;
  DWORD i,j,k,addr,retl;
  NTSTATUS status;
  qsort(&list->WorkingSetList, list->NumberOfPages, 4, cmp);
  base = list->WorkingSetList[0];
  size = 0x1000;
  for (i = 1; i < list->NumberOfPages; i++) {
    if ((list->WorkingSetList[i] & 2) == 0)
      continue;
    if (list->WorkingSetList[i] >> 31)
      break;
    if (base + size == list->WorkingSetList[i])
      size += 0x1000;
    else {
      if (size > 0x2000) {
        addr = base & ~0xFFF;
        status = NtQueryVirtualMemory(NtCurrentProcess(),(PVOID)addr,
          MemorySectionName,text_buffer_prev,0x1000,&retl);
        if (!NT_SUCCESS(status)) {
          k = addr + size - 4;
          for (j = addr; j < k; j++) {
            if (*(DWORD*)j == 0x5044b70f) {
              if (*(WORD*)(j + 4) == 0x890c) { // movzx eax, word ptr [edx*2 + eax + 0xC]; wchar = string[i];
                HookParam hp = {};
                hp.addr = j;
                hp.extern_fun = SpecialHookAB2Try;
                hp.type = USING_STRING | USING_UNICODE| EXTERN_HOOK | NO_CONTEXT;
                ConsoleOutput("vnreng: INSERT AB2Try");
                NewHook(hp, L"AB2Try");
                //ConsoleOutput("Please adjust text speed to fastest/immediate.");
                //RegisterEngineType(ENGINE_AB2T);
                return TRUE;
              }
            }
          }
        }
      }
      size = 0x1000;
      base = list->WorkingSetList[i];
    }
  }
  return FALSE;
}
void InsertAB2TryHook()
{
  DWORD size = 0;
  MEMORY_WORKING_SET_LIST *list = GetWorkingSet();
  if (list == 0) {
    ConsoleOutput("vnreng:AB2Try: cannot find working list");
    return;
  }
  if (!FindCharacteristInstruction(list))
    ConsoleOutput("vnreng:AB2Try: cannot find characteristic sequence");
    //L"Make sure you have start the game and have seen some text on the screen.");
  NtFreeVirtualMemory(NtCurrentProcess(), (PVOID*)&list, &size, MEM_RELEASE);
}
/********************************************************************************************
C4 hook: (Contributed by Stomp)
  Game folder contains C4.EXE or XEX.EXE.
********************************************************************************************/
bool InsertC4Hook()
{
  BYTE sig[8]={0x8A, 0x10, 0x40, 0x80, 0xFA, 0x5F, 0x88, 0x15};
  DWORD i = SearchPattern(module_base_,module_limit_-module_base_,sig,8);
  if (!i) {
    ConsoleOutput("vnreng:C4: pattern not found");
    return false;
  }
  HookParam hp = {};
  hp.addr = i + module_base_;
  hp.off = -0x08;
  hp.type |= DATA_INDIRECT|NO_CONTEXT;
  hp.length_offset = 1;
  ConsoleOutput("vnreng: INSERT C4");
  NewHook(hp, L"C4");
  //RegisterEngineType(ENGINE_C4);
  return true;
}
static void SpecialHookWillPlus(DWORD esp_base, HookParam* hp, DWORD* data, DWORD* split, DWORD* len)
{

  static DWORD detect_offset;
  if (detect_offset) return;
  DWORD i,l;
  union{
    DWORD retn;
    WORD *pw;
    BYTE *pb;
  };
  retn = *(DWORD*)esp_base;
  i = 0;
  while (*pw != 0xc483) { //add esp, $
    l = disasm(pb);
    if (++i == 5)
      //ConsoleOutput("Fail to detect offset.");
      break;
    retn += l;
  }
  if (*pw == 0xC483) {
    hp->off = *(pb + 2) - 8;
    hp->type ^= EXTERN_HOOK;
    hp->extern_fun = 0;
    char* str = *(char**)(esp_base + hp->off);
    *data = (DWORD)str;
    *len = strlen(str);
    *split = 0;
  }
  detect_offset = 1;
}

bool InsertWillPlusHook()
{
  //__debugbreak();
  DWORD addr = Util::FindCallAndEntryAbs((DWORD)GetGlyphOutlineA,module_limit_-module_base_,module_base_,0xec81);
  if (!addr) {
    ConsoleOutput("vnreng:WillPlus: function call not found");
    return false;
  }

  HookParam hp = {};
  hp.addr = addr;
  hp.extern_fun = SpecialHookWillPlus;
  hp.type = USING_STRING | EXTERN_HOOK;
  ConsoleOutput("vnreng: INSERT WillPlus");
  NewHook(hp, L"WillPlus");
  //RegisterEngineType(ENGINE_WILLPLUS);
  return true;
}

/** jichi 9/14/2013
 *  TanukiSoft (*.tac)
 *
 *  Seems to be broken for new games in 2012 such like となりの
 *
 *  微少女: /HSN4@004983E0
 *  This is the same hook as ITH
 *  - addr: 4817888 (0x4983e0)
 *  - extern_fun: 0x0
 *  - off: 4
 *  - type: 1025 (0x401)
 *
 *  隣りのぷ～さん: /HSN-8@200FE7:TONARINO.EXE
 *  - addr: 2101223 (0x200fe7)
 *  - module: 2343491905 (0x8baed941)
 *  - off: 4294967284 (0xfffffff4, -0xc)
 *  - type: 1089 (0x441)
 */
bool InsertTanukiHook()
{
  ConsoleOutput("vnreng: trying TanukiSoft");
  for (DWORD i = module_base_; i < module_limit_ - 4; i++)
    if (*(DWORD *)i == 0x8140)
      if (DWORD j = SafeFindEntryAligned(i,0x400)) { // jichi 9/14/2013: might crash the game without admin priv
        //ITH_GROWL_DWORD2(i, j);
        HookParam hp = {};
        hp.addr = j;
        hp.off = 4;
        hp.type = USING_STRING | NO_CONTEXT;
        ConsoleOutput("vnreng: INSERT TanukiSoft");
        NewHook(hp, L"TanukiSoft");
        return true;
      }

  //ConsoleOutput("Unknown TanukiSoft engine.");
  ConsoleOutput("vnreng:TanukiSoft: failed");
  return false;
}
static void SpecialHookRyokucha(DWORD esp_base, HookParam* hp, DWORD* data, DWORD* split, DWORD* len)
{
  DWORD *base = (DWORD*)esp_base;
  DWORD i, j;
  for (i = 1; i < 5; i++)
  {
    j = base[i];
    if ((j >> 16) == 0 && (j >> 8))
    {
      hp->off = i << 2;
      *data = j;
      *len = 2;
      hp->type &= ~EXTERN_HOOK;
      hp->extern_fun = 0;
      return;
    }
  }
  *len = 0;
}
bool InsertRyokuchaDynamicHook(LPVOID addr, DWORD frame, DWORD stack)
{
  if (addr != GetGlyphOutlineA) return false;
  bool flag;
  DWORD insert_addr;
  __asm
  {
    mov eax,fs:[0]
    mov eax,[eax]
    mov eax,[eax] //Step up SEH chain for 2 nodes.
    mov ecx,[eax + 0xC]
    mov eax,[eax + 4]
    add ecx,[ecx - 4]
    mov insert_addr,ecx
    cmp eax,[ecx + 3]
    sete al
    mov flag,al
  }
  if (flag) {
    HookParam hp = {};
    hp.addr = insert_addr;
    hp.length_offset = 1;
    hp.extern_fun = SpecialHookRyokucha;
    hp.type = BIG_ENDIAN | EXTERN_HOOK;
    ConsoleOutput("vnreng: INSERT StudioRyokucha");
    NewHook(hp, L"StudioRyokucha");
  }
  //else ConsoleOutput("Unknown Ryokucha engine.");
  ConsoleOutput("vnreng:StudioRyokucha: failed");
  return true;
}
void InsertRyokuchaHook()
{
  //ConsoleOutput("Probably Ryokucha. Wait for text.");
  trigger_fun_ = InsertRyokuchaDynamicHook;
  SwitchTrigger(true);
  ConsoleOutput("vnreng: TRIGGER Ryokucha");
}
bool InsertGXPHook()
{
  union {
    DWORD i;
    DWORD *id;
    BYTE *ib;
  };
  //__asm int 3
  for (i = module_base_ + 0x1000; i < module_limit_ - 4; i++) {
    //find cmp word ptr [esi*2+eax],0
    if (*id != 0x703c8366)
      continue;
    i += 4;
    if (*ib != 0)
      continue;
    i++;
    DWORD j = i + 0x200;
    j = j < (module_limit_ - 8) ? j : (module_limit_ - 8);

    DWORD flag = false;
    while (i < j) {
      DWORD k = disasm(ib);
      if (k == 0)
        break;
      if (k == 1 && (*ib & 0xf8) == 0x50) { // push reg
        flag = true;
        break;
      }
      i += k;
    }
    if (flag)
      while (i < j) {
        if (*ib == 0xe8) {
          i++;
          DWORD addr = *id + i + 4;
          if (addr > module_base_ && addr < module_limit_) {
            HookParam hp = {};
            hp.addr = addr;
            hp.type = USING_UNICODE | DATA_INDIRECT;
            hp.length_offset = 1;
            hp.off = 4;
            ConsoleOutput("vnreng: INSERT GXP");
            NewHook(hp, L"GXP");
            return true;
          }
        }
        i++;
      }
  }
  //ConsoleOutput("Unknown GXP engine.");
  ConsoleOutput("vnreng:GXP: failed");
  return false;
}
static BYTE JIS_tableH[0x80] = {
  0x00,0x81,0x81,0x82,0x82,0x83,0x83,0x84,
  0x84,0x85,0x85,0x86,0x86,0x87,0x87,0x88,
  0x88,0x89,0x89,0x8A,0x8A,0x8B,0x8B,0x8C,
  0x8C,0x8D,0x8D,0x8E,0x8E,0x8F,0x8F,0x90,
  0x90,0x91,0x91,0x92,0x92,0x93,0x93,0x94,
  0x94,0x95,0x95,0x96,0x96,0x97,0x97,0x98,
  0x98,0x99,0x99,0x9A,0x9A,0x9B,0x9B,0x9C,
  0x9C,0x9D,0x9D,0x9E,0x9E,0xDF,0xDF,0xE0,
  0xE0,0xE1,0xE1,0xE2,0xE2,0xE3,0xE3,0xE4,
  0xE4,0xE5,0xE5,0xE6,0xE6,0xE7,0xE7,0xE8,
  0xE8,0xE9,0xE9,0xEA,0xEA,0xEB,0xEB,0xEC,
  0xEC,0xED,0xED,0xEE,0xEE,0xEF,0xEF,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

static BYTE JIS_tableL[0x80] = {
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x40,0x41,0x42,0x43,0x44,0x45,0x46,
  0x47,0x48,0x49,0x4A,0x4B,0x4C,0x4D,0x4E,
  0x4F,0x50,0x51,0x52,0x53,0x54,0x55,0x56,
  0x57,0x58,0x59,0x5A,0x5B,0x5C,0x5D,0x5E,
  0x5F,0x60,0x61,0x62,0x63,0x64,0x65,0x66,
  0x67,0x68,0x69,0x6A,0x6B,0x6C,0x6D,0x6E,
  0x6F,0x70,0x71,0x72,0x73,0x74,0x75,0x76,
  0x77,0x78,0x79,0x7A,0x7B,0x7C,0x7D,0x7E,
  0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,
  0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,
  0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,
  0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x00,
};

static void SpecialHookAnex86(DWORD esp_base, HookParam* hp, DWORD* data, DWORD* split, DWORD* len)
{
  __asm
  {
    mov eax, esp_base
    mov ecx, [eax - 0xC]
    cmp byte ptr [ecx + 0xE], 0
    jnz _fin
    movzx ebx, byte ptr [ecx + 0xC] ; Low byte
    movzx edx, byte ptr [ecx + 0xD] ; High byte
    test edx,edx
    jnz _jis_char
    mov eax,data
    mov [eax],ebx
    mov eax, len
    mov [eax], 1
    jmp _fin
_jis_char:
    cmp ebx,0x7E
    ja _fin
    cmp edx,0x7E
    ja _fin
    test dl,1
    lea eax, [ebx + 0x7E]
    movzx ecx, byte ptr [JIS_tableL + ebx]
    cmovnz eax, ecx
    mov ah, byte ptr [JIS_tableH + edx]
    ror ax,8
    mov ecx, data
    mov [ecx], eax
    mov ecx, len
    mov [ecx], 2
_fin:
  }

}
bool InsertAnex86Hook()
{
  const DWORD inst[] = {0x618ac033,0x0d418a0c}; // jichi 12/25/2013: Remove static keyword
  for (DWORD i = module_base_ + 0x1000; i < module_limit_ - 8; i++)
    if (*(DWORD *)i == inst[0])
      if (*(DWORD *)(i + 4) == inst[1]) {
        HookParam hp = {};
        hp.addr = i;
        hp.extern_fun = SpecialHookAnex86;
        hp.type = EXTERN_HOOK;
        hp.length_offset = 1;
        ConsoleOutput("vnreng: INSERT Anex86");
        NewHook(hp, L"Anex86");
        return true;
      }
  ConsoleOutput("vnreng:Anex86: failed");
  return false;
}
//static char* ShinyDaysQueueString[0x10];
//static int ShinyDaysQueueStringLen[0x10];
//static int ShinyDaysQueueIndex, ShinyDaysQueueNext;
static int ShinyDaysQueueStringLen;
static void SpecialHookShinyDays(DWORD esp_base, HookParam* hp, DWORD* data, DWORD* split, DWORD* len)
{
  LPWSTR fun_str;
  char *text_str;
  DWORD l = 0;
  __asm
  {
    mov eax,esp_base
    mov ecx,[eax+0x4C]
    mov fun_str,ecx
    mov esi,[eax+0x70]
    mov edi,[eax+0x74]
    add esi,0x3C
    cmp esi,edi
    jae _no_text
    mov edx,[esi+0x10]
    mov ecx,esi
    cmp edx,8
    cmovae ecx,[ecx]
    add edx,edx
    mov text_str,ecx
    mov l,edx
_no_text:
  }
  if (memcmp(fun_str, L"[PlayVoice]",0x18) == 0) {
    *data = (DWORD)text_buffer;
    *len = ShinyDaysQueueStringLen;
  }
  else if (memcmp(fun_str, L"[PrintText]",0x18) == 0) {
    memcpy(text_buffer, text_str, l);
    ShinyDaysQueueStringLen = l;
  }
}
bool InsertShinyDaysHook()
{
  const BYTE ins[0x10] = {
    0xff,0x83,0x70,0x03,0x00,0x00,0x33,0xf6,
    0xc6,0x84,0x24,0x90,0x02,0x00,0x00,0x02
  };
  LPVOID addr = (LPVOID)0x42ad94;
  if (memcmp(addr, ins, 0x10) != 0) {
    ConsoleOutput("vnreng:ShinyDays: only work for 1.00");
    return false;
  }

  HookParam hp = {};
  hp.addr = 0x42ad9c;
  hp.extern_fun = SpecialHookShinyDays;
  hp.type = USING_UNICODE | USING_STRING| EXTERN_HOOK | NO_CONTEXT;
  ConsoleOutput("vnreng: INSERT ShinyDays");
  NewHook(hp, L"ShinyDays 1.00");
  return true;
}

/**
 *  jichi 9/5/2013: NEXTON games with aInfo.db
 *  Sample games:
 *  - /HA-C@4D69E:InnocentBullet.exe (イノセントバレット)
 *  - /HA-C@40414C:ImoutoBancho.exe (妹番長)
 *
 *  See: http://ja.wikipedia.org/wiki/ネクストン
 *  See (CaoNiMaGeBi): http://tieba.baidu.com/p/2576241908
 *
 *  Old:
 *  md5 = 85ac031f2539e1827d9a1d9fbde4023d
 *  hcode = /HA-C@40414C:ImoutoBancho.exe
 *  - addr: 4211020 (0x40414c)
 *  - module: 1051997988 (0x3eb43724)
 *  - length_offset: 1
 *  - off: 4294967280 (0xfffffff0) = -0x10
 *  - split: 0
 *  - type: 68 (0x44)
 *
 *  New (11/7/2013):
 *  /HA-20:4@583DE:MN2.EXE (NEW)
 *  - addr: 361438 (0x583de)
 *  - module: 3436540819
 *  - length_offset: 1
 *  - off: 4294967260 (0xffffffdc) = -0x24
 *  - split: 4
 *  - type: 84 (0x54)
 */

bool InsertNextonHook()
{
  // 0x8944241885c00f84
  const BYTE ins[] = {
    //0xe8 //??,??,??,??,      00804147   e8 24d90100      call imoutoba.00821a70
    0x89,0x44,0x24, 0x18,   // 0080414c   894424 18        mov dword ptr ss:[esp+0x18],eax; hook here
    0x85,0xc0,              // 00804150   85c0             test eax,eax
    0x0f,0x84               // 00804152  ^0f84 c0feffff    je imoutoba.00804018
  };
  //enum { hook_offset = 0 };
  ULONG addr = module_base_; //- sizeof(ins);
  do {
    addr += sizeof(ins); // ++ so that each time return diff address
    ULONG range = min(module_limit_ - addr, MAX_REL_ADDR);
    ULONG offset = SearchPattern(addr, range, ins, sizeof(ins));
    if (!offset) {
      ConsoleOutput("vnreng:NEXTON: pattern not exist");
      return false;
    }

    addr += offset;

    //const BYTE hook_ins[] = {
    //  0x57,       // 00804144   57               push edi
    //  0x8b,0xc3,  // 00804145   8bc3             mov eax,ebx
    //  0xe8 //??,??,??,??,      00804147   e8 24d90100      call imoutoba.00821a70
    //};
  } while(0xe8c38b57 != *(DWORD *)(addr - 8));

  //ITH_GROWL_DWORD3(module_base_, addr, *(DWORD *)(addr-8));

  HookParam hp = {};
  hp.addr = addr;
  hp.length_offset = 1;
  //hp.off = -0x10;
  //hp.type = BIG_ENDIAN; // 4

  // 魔王のくせに生イキだっ！2 ～今度は性戦だ！～
  // CheatEngine search for byte array: 8944241885C00F84
  //addr = 0x4583de; // wrong
  //addr = 0x5460ba;
  //addr = 0x5f3d8a;
  //addr = 0x768776;
  //addr = 0x7a5319;

  hp.off = -0x24;
  hp.split = 4;
  hp.type = BIG_ENDIAN|USING_SPLIT; // 0x54

  // Indirect is needed for new games,
  // Such as: /HA-C*0@4583DE for 「魔王のくせに生イキだっ！２」
  //hp.type = BIG_ENDIAN|DATA_INDIRECT; // 12
  //hp.type = USING_UNICODE;
  //ITH_GROWL_DWORD3(addr, -hp.off, hp.type);

  ConsoleOutput("vnreng: INSERT NEXTON");
  NewHook(hp, L"NEXTON");

  //ConsoleOutput("NEXTON");
  return true;
}

/**
 *  jichi 9/16/2013: a-unicorn / gesen18
 *  See (CaoNiMaGeBi): http://tieba.baidu.com/p/2586681823
 *  Pattern: 2bce8bf8
 *      2bce      sub ecx,esi ; hook here
 *      8bf8      mov eds,eax
 *      8bd1      mov edx,ecx
 *
 *  /HBN-20*0@xxoo
 *  - length_offset: 1
 *  - off: 4294967260 (0xffffffdc)
 *  - type: 1032 (0x408)
 */
bool InsertGesen18Hook()
{
  const BYTE ins[] = {
    0x2b,0xce,  // sub ecx,esi ; hook here
    0x8b,0xf8   // mov eds,eax
  };
  enum { hook_offset = 0 };
  ULONG range = min(module_limit_ - module_base_, MAX_REL_ADDR);
  ULONG reladdr = SearchPattern(module_base_, range, ins, sizeof(ins));
  if (!reladdr) {
    ConsoleOutput("vnreng:Gesen18: pattern not exist");
    return false;
  }

  HookParam hp = {};
  hp.type = NO_CONTEXT | DATA_INDIRECT;
  hp.length_offset = 1;
  hp.off = -0x24;
  hp.addr = module_base_ + reladdr + hook_offset;

  //index = SearchPattern(module_base_, size,ins, sizeof(ins));
  //ITH_GROWL_DWORD2(base, index);

  ConsoleOutput("vnreng: INSERT Gesen18");
  NewHook(hp, L"Gesen18");
  //ConsoleOutput("Gesen18");
  return true;
}

/**
 *  jichi 10/1/2013: Artemis Engine
 *  See: http://www.ies-net.com/
 *  See (CaoNiMaGeBi): http://tieba.baidu.com/p/2625537737
 *  Pattern:
 *     650a2f 83c4 0c   add esp,0xc ; hook here
 *     650a32 0fb6c0    movzx eax,al
 *     650a35 85c0      test eax,eax
 *     0fb6c0 75 0e     jnz short tsugokaz.0065a47
 *
 *  Wrong: 0x400000 + 0x7c574
 *
 *  //Example: [130927]妹スパイラル /HBN-8*0:14@65589F
 *  Example: ツゴウノイイ家族 Trial /HBN-8*0:14@650A2F
 *  Note: 0x650a2f > 40000(base) + 20000(limit)
 *  - addr: 0x650a2f
 *  - extern_fun: 0x0
 *  - function: 0
 *  - hook_len: 0
 *  - ind: 0
 *  - length_offset: 1
 *  - module: 0
 *  - off: 4294967284 = 0xfffffff4 = -0xc
 *  - recover_len: 0
 *  - split: 20 = 0x14
 *  - split_ind: 0
 *  - type: 1048 = 0x418
 *
 *  @CaoNiMaGeBi:
 *  RECENT GAMES:
 *    [130927]妹スパイラル /HBN-8*0:14@65589F
 *    [130927]サムライホルモン
 *    [131025]ツゴウノイイ家族 /HBN-8*0:14@650A2F (for trial version)
 *    CLIENT ORGANIZAIONS:
 *    CROWD
 *    D:drive.
 *    Hands-Aid Corporation
 *    iMel株式会社
 *    SHANNON
 *    SkyFish
 *    SNACK-FACTORY
 *    team flap
 *    Zodiac
 *    くらむちゃうだ～
 *    まかろんソフト
 *    アイディアファクトリー株式会社
 *    カラクリズム
 *    合资会社ファーストリーム
 *    有限会社ウルクスへブン
 *    有限会社ロータス
 *    株式会社CUCURI
 *    株式会社アバン
 *    株式会社インタラクティブブレインズ
 *    株式会社ウィンディール
 *    株式会社エヴァンジェ
 *    株式会社ポニーキャニオン
 *    株式会社大福エンターテインメント
 */
bool InsertArtemisHook()
{
  const BYTE ins[] = {
    0x83,0xc4, 0x0c, // add esp,0xc ; hook here
    0x0f,0xb6,0xc0,  // movzx eax,al
    0x85,0xc0,       // test eax,eax
    0x75, 0x0e       // jnz XXOO ; it must be 0xe, or there will be duplication
  };
  enum { hook_offset = 0 };
  ULONG range = min(module_limit_ - module_base_, MAX_REL_ADDR);
  ULONG reladdr = SearchPattern(module_base_, range, ins, sizeof(ins));
  //ITH_GROWL_DWORD3(reladdr, module_base_, range);
  if (!reladdr) {
    ConsoleOutput("vnreng:Artemis: pattern not exist");
    return false;
  }

  HookParam hp = {};
  hp.addr = module_base_ + reladdr + hook_offset;
  hp.length_offset = 1;
  hp.off = -0xc;
  hp.split = 0x14;
  hp.type = NO_CONTEXT | DATA_INDIRECT | USING_SPLIT; // 0x418

  //hp.addr = 0x650a2f;
  //ITH_GROWL_DWORD(hp.addr);

  ConsoleOutput("vnreng: INSERT Artemis");
  NewHook(hp, L"Artemis");
  //ConsoleOutput("Artemis");
  return true;
}

/**
 *  jichi 1/2/2014: Taskforce2 Engine
 *
 *  Examples:
 *  神様(仮)-カミサマカッコカリ- 路地裏繚乱編 (1.1)
 *  /HS-8@178872:Taskforce2.exe
 *
 *  00578819   . 50             PUSH EAX                                 ; |Arg1
 *  0057881A   . C745 F4 CC636B>MOV DWORD PTR SS:[EBP-0xC],Taskforc.006B>; |
 *  00578821   . E8 31870000    CALL Taskforc.00580F57                   ; \Taskforc.00580F57
 *  00578826   . CC             INT3
 *  00578827  /$ 8B4C24 04      MOV ECX,DWORD PTR SS:[ESP+0x4]
 *  0057882B  |. 53             PUSH EBX
 *  0057882C  |. 33DB           XOR EBX,EBX
 *  0057882E  |. 3BCB           CMP ECX,EBX
 *  00578830  |. 56             PUSH ESI
 *  00578831  |. 57             PUSH EDI
 *  00578832  |. 74 08          JE SHORT Taskforc.0057883C
 *  00578834  |. 8B7C24 14      MOV EDI,DWORD PTR SS:[ESP+0x14]
 *  00578838  |. 3BFB           CMP EDI,EBX
 *  0057883A  |. 77 1B          JA SHORT Taskforc.00578857
 *  0057883C  |> E8 28360000    CALL Taskforc.0057BE69
 *  00578841  |. 6A 16          PUSH 0x16
 *  00578843  |. 5E             POP ESI
 *  00578844  |. 8930           MOV DWORD PTR DS:[EAX],ESI
 *  00578846  |> 53             PUSH EBX
 *  00578847  |. 53             PUSH EBX
 *  00578848  |. 53             PUSH EBX
 *  00578849  |. 53             PUSH EBX
 *  0057884A  |. 53             PUSH EBX
 *  0057884B  |. E8 6A050000    CALL Taskforc.00578DBA
 *  00578850  |. 83C4 14        ADD ESP,0x14
 *  00578853  |. 8BC6           MOV EAX,ESI
 *  00578855  |. EB 31          JMP SHORT Taskforc.00578888
 *  00578857  |> 8B7424 18      MOV ESI,DWORD PTR SS:[ESP+0x18]
 *  0057885B  |. 3BF3           CMP ESI,EBX
 *  0057885D  |. 75 04          JNZ SHORT Taskforc.00578863
 *  0057885F  |. 8819           MOV BYTE PTR DS:[ECX],BL
 *  00578861  |.^EB D9          JMP SHORT Taskforc.0057883C
 *  00578863  |> 8BD1           MOV EDX,ECX
 *  00578865  |> 8A06           /MOV AL,BYTE PTR DS:[ESI]
 *  00578867  |. 8802           |MOV BYTE PTR DS:[EDX],AL
 *  00578869  |. 42             |INC EDX
 *  0057886A  |. 46             |INC ESI
 *  0057886B  |. 3AC3           |CMP AL,BL
 *  0057886D  |. 74 03          |JE SHORT Taskforc.00578872
 *  0057886F  |. 4F             |DEC EDI
 *  00578870  |.^75 F3          \JNZ SHORT Taskforc.00578865
 *  00578872  |> 3BFB           CMP EDI,EBX ; jichi: hook here
 *  00578874  |. 75 10          JNZ SHORT Taskforc.00578886
 *  00578876  |. 8819           MOV BYTE PTR DS:[ECX],BL
 *  00578878  |. E8 EC350000    CALL Taskforc.0057BE69
 *  0057887D  |. 6A 22          PUSH 0x22
 *  0057887F  |. 59             POP ECX
 *  00578880  |. 8908           MOV DWORD PTR DS:[EAX],ECX
 *  00578882  |. 8BF1           MOV ESI,ECX
 *  00578884  |.^EB C0          JMP SHORT Taskforc.00578846
 *  00578886  |> 33C0           XOR EAX,EAX
 *  00578888  |> 5F             POP EDI
 *  00578889  |. 5E             POP ESI
 *  0057888A  |. 5B             POP EBX
 *  0057888B  \. C3             RETN
 *
 *  [131129] [Digital Cute] オトメスイッチ -OtomeSwitch- ～彼が持ってる彼女のリモコン～ (1.1)
 *  /HS-8@1948E9:Taskforce2.exe
 *  - addr: 0x1948e9
 *  - off: 4294967284 (0xfffffff4 = -0xc)
 *  - type: 65  (0x41)
 *
 *  00594890   . 50             PUSH EAX                                 ; |Arg1
 *  00594891   . C745 F4 64C56D>MOV DWORD PTR SS:[EBP-0xC],Taskforc.006D>; |
 *  00594898   . E8 88880000    CALL Taskforc.0059D125                   ; \Taskforc.0059D125
 *  0059489D   . CC             INT3
 *  0059489E  /$ 8B4C24 04      MOV ECX,DWORD PTR SS:[ESP+0x4]
 *  005948A2  |. 53             PUSH EBX
 *  005948A3  |. 33DB           XOR EBX,EBX
 *  005948A5  |. 3BCB           CMP ECX,EBX
 *  005948A7  |. 56             PUSH ESI
 *  005948A8  |. 57             PUSH EDI
 *  005948A9  |. 74 08          JE SHORT Taskforc.005948B3
 *  005948AB  |. 8B7C24 14      MOV EDI,DWORD PTR SS:[ESP+0x14]
 *  005948AF  |. 3BFB           CMP EDI,EBX
 *  005948B1  |. 77 1B          JA SHORT Taskforc.005948CE
 *  005948B3  |> E8 91350000    CALL Taskforc.00597E49
 *  005948B8  |. 6A 16          PUSH 0x16
 *  005948BA  |. 5E             POP ESI
 *  005948BB  |. 8930           MOV DWORD PTR DS:[EAX],ESI
 *  005948BD  |> 53             PUSH EBX
 *  005948BE  |. 53             PUSH EBX
 *  005948BF  |. 53             PUSH EBX
 *  005948C0  |. 53             PUSH EBX
 *  005948C1  |. 53             PUSH EBX
 *  005948C2  |. E8 7E010000    CALL Taskforc.00594A45
 *  005948C7  |. 83C4 14        ADD ESP,0x14
 *  005948CA  |. 8BC6           MOV EAX,ESI
 *  005948CC  |. EB 31          JMP SHORT Taskforc.005948FF
 *  005948CE  |> 8B7424 18      MOV ESI,DWORD PTR SS:[ESP+0x18]
 *  005948D2  |. 3BF3           CMP ESI,EBX
 *  005948D4  |. 75 04          JNZ SHORT Taskforc.005948DA
 *  005948D6  |. 8819           MOV BYTE PTR DS:[ECX],BL
 *  005948D8  |.^EB D9          JMP SHORT Taskforc.005948B3
 *  005948DA  |> 8BD1           MOV EDX,ECX
 *  005948DC  |> 8A06           /MOV AL,BYTE PTR DS:[ESI]
 *  005948DE  |. 8802           |MOV BYTE PTR DS:[EDX],AL
 *  005948E0  |. 42             |INC EDX
 *  005948E1  |. 46             |INC ESI
 *  005948E2  |. 3AC3           |CMP AL,BL
 *  005948E4  |. 74 03          |JE SHORT Taskforc.005948E9
 *  005948E6  |. 4F             |DEC EDI
 *  005948E7  |.^75 F3          \JNZ SHORT Taskforc.005948DC
 *  005948E9  |> 3BFB           CMP EDI,EBX ; jichi: hook here
 *  005948EB  |. 75 10          JNZ SHORT Taskforc.005948FD
 *  005948ED  |. 8819           MOV BYTE PTR DS:[ECX],BL
 *  005948EF  |. E8 55350000    CALL Taskforc.00597E49
 *  005948F4  |. 6A 22          PUSH 0x22
 *  005948F6  |. 59             POP ECX
 *  005948F7  |. 8908           MOV DWORD PTR DS:[EAX],ECX
 *  005948F9  |. 8BF1           MOV ESI,ECX
 *  005948FB  |.^EB C0          JMP SHORT Taskforc.005948BD
 *  005948FD  |> 33C0           XOR EAX,EAX
 *  005948FF  |> 5F             POP EDI
 *  00594900  |. 5E             POP ESI
 *  00594901  |. 5B             POP EBX
 *  00594902  \. C3             RETN
 *
 *  Use this if that hook fails, try this one for future engines:
 *  /HS0@44CADA
 */
bool InsertTaskforce2Hook()
{
  const BYTE ins[] = {
    0x88,0x02,  // 005948de  |. 8802           |mov byte ptr ds:[edx],al
    0x42,       // 005948e0  |. 42             |inc edx
    0x46,       // 005948e1  |. 46             |inc esi
    0x3a,0xc3,  // 005948e2  |. 3ac3           |cmp al,bl
    0x74, 0x03, // 005948e4  |. 74 03          |je short taskforc.005948e9
    0x4f,       // 005948e6  |. 4f             |dec edi
    0x75, 0xf3, // 005948e7  |.^75 f3          \jnz short taskforc.005948dc
    0x3b,0xfb   // 005948e9  |> 3bfb           cmp edi,ebx ; jichi: hook here
  };
  enum { hook_offset = sizeof(ins) - 2 };
  ULONG range = min(module_limit_ - module_base_, MAX_REL_ADDR);
  ULONG reladdr = SearchPattern(module_base_, range, ins, sizeof(ins));
  //ITH_GROWL_DWORD3(reladdr, module_base_, range);
  if (!reladdr) {
    ConsoleOutput("vnreng:Taskforce2: pattern not exist");
    //return false;
  }

  HookParam hp = {};
  hp.addr = module_base_ + reladdr + hook_offset;
  hp.off = -0xc;
  hp.type = BIG_ENDIAN | USING_STRING; // 0x41

  //ITH_GROWL_DWORD(hp.addr);
  //hp.addr = 0x1948e9 + module_base_;

  ConsoleOutput("vnreng: INSERT Taskforce2");
  NewHook(hp, L"Taskforce2");
  return true;
}

namespace { // unnamed Rejet
/**
 *  jichi 12/22/2013: Rejet
 *  See (CaoNiMaGeBi): http://www.hongfire.com/forum/printthread.php?t=36807&pp=40&page=172
 *  See (CaoNiMaGeBi): http://tieba.baidu.com/p/2506179113
 *  Pattern: 2bce8bf8
 *      2bce      sub ecx,esi ; hook here
 *      8bf8      mov eds,eax
 *      8bd1      mov edx,ecx
 *
 *  Examples:
 *  - Type1: ドットカレシ-We're 8bit Lovers!: /HBN-4*0@A5332:DotKareshi.exe
 *    length_offset: 1
 *    off: 0xfffffff8 (-0x8)
 *    type: 1096 (0x448)
 *
 *    module_base_ = 10e0000 (variant)
 *    hook_addr = module_base_ + reladdr = 0xe55332
 *    01185311   . FFF0           PUSH EAX  ; beginning of a new functino
 *    01185313   . 0FC111         XADD DWORD PTR DS:[ECX],EDX
 *    01185316   . 4A             DEC EDX
 *    01185317   . 85D2           TEST EDX,EDX
 *    01185319   . 0F8F 45020000  JG DotKares.01185564
 *    0118531F   . 8B08           MOV ECX,DWORD PTR DS:[EAX]
 *    01185321   . 8B11           MOV EDX,DWORD PTR DS:[ECX]
 *    01185323   . 50             PUSH EAX
 *    01185324   . 8B42 04        MOV EAX,DWORD PTR DS:[EDX+0x4]
 *    01185327   . FFD0           CALL EAX
 *    01185329   . E9 36020000    JMP DotKares.01185564
 *    0118532E   . 8B7424 20      MOV ESI,DWORD PTR SS:[ESP+0x20]
 *    01185332   . E8 99A9FBFF    CALL DotKares.0113FCD0    ; hook here
 *    01185337   . 8BF0           MOV ESI,EAX
 *    01185339   . 8D4C24 14      LEA ECX,DWORD PTR SS:[ESP+0x14]
 *    0118533D   . 3BF7           CMP ESI,EDI
 *    0118533F   . 0F84 1A020000  JE DotKares.0118555F
 *    01185345   . 51             PUSH ECX                                 ; /Arg2
 *    01185346   . 68 E4FE5501    PUSH DotKares.0155FEE4                   ; |Arg1 = 0155FEE4
 *    0118534B   . E8 1023F9FF    CALL DotKares.01117660                   ; \DotKares.00377660
 *    01185350   . 83C4 08        ADD ESP,0x8
 *    01185353   . 84C0           TEST AL,AL
 *
 *  - Type2: ドットカレシ-We're 8bit Lovers! II: /HBN-8*0@A7AF9:dotkareshi.exe
 *    off: 4294967284 (0xfffffff4 = -0xc)
 *    length_offset: 1
 *    type: 1096 (0x448)
 *
 *    module_base_: 0x12b0000
 *
 *    01357AD2   . FFF0           PUSH EAX ; beginning of a new functino
 *    01357AD4   . 0FC111         XADD DWORD PTR DS:[ECX],EDX
 *    01357AD7   . 4A             DEC EDX
 *    01357AD8   . 85D2           TEST EDX,EDX
 *    01357ADA   . 7F 0A          JG SHORT DotKares.01357AE6
 *    01357ADC   . 8B08           MOV ECX,DWORD PTR DS:[EAX]
 *    01357ADE   . 8B11           MOV EDX,DWORD PTR DS:[ECX]
 *    01357AE0   . 50             PUSH EAX
 *    01357AE1   . 8B42 04        MOV EAX,DWORD PTR DS:[EDX+0x4]
 *    01357AE4   . FFD0           CALL EAX
 *    01357AE6   > 8B4C24 14      MOV ECX,DWORD PTR SS:[ESP+0x14]
 *    01357AEA   . 33FF           XOR EDI,EDI
 *    01357AEC   . 3979 F4        CMP DWORD PTR DS:[ECX-0xC],EDI
 *    01357AEF   . 0F84 1E020000  JE DotKares.01357D13
 *    01357AF5   . 8B7424 20      MOV ESI,DWORD PTR SS:[ESP+0x20]
 *    01357AF9   . E8 7283FBFF    CALL DotKares.0130FE70    ; jichi: hook here
 *    01357AFE   . 8BF0           MOV ESI,EAX
 *    01357B00   . 3BF7           CMP ESI,EDI
 *    01357B02   . 0F84 0B020000  JE DotKares.01357D13
 *    01357B08   . 8D5424 14      LEA EDX,DWORD PTR SS:[ESP+0x14]
 *    01357B0C   . 52             PUSH EDX                                 ; /Arg2
 *    01357B0D   . 68 CC9F7501    PUSH DotKares.01759FCC                   ; |Arg1 = 01759FCC
 *    01357B12   . E8 E9F9F8FF    CALL DotKares.012E7500                   ; \DotKares.012C7500
 *    01357B17   . 83C4 08        ADD ESP,0x8
 *    01357B1A   . 84C0           TEST AL,AL
 *    01357B1C   . 74 1D          JE SHORT DotKares.01357B3B
 *    01357B1E   . 8D46 64        LEA EAX,DWORD PTR DS:[ESI+0x64]
 *    01357B21   . E8 BAD0F8FF    CALL DotKares.012E4BE0
 *    01357B26   . 68 28A17501    PUSH DotKares.0175A128                   ; /Arg1 = 0175A128 ASCII "<br>"
 *
 *  - Type2: Tiny×MACHINEGUN: /HBN-8*0@4CEB8:TinyMachinegun.exe
 *    module_base_: 0x12f0000
 *    There are two possible places to hook
 *
 *    0133CEA0   . FFF0           PUSH EAX ; beginning of a new functino
 *    0133CEA2   . 0FC111         XADD DWORD PTR DS:[ECX],EDX
 *    0133CEA5   . 4A             DEC EDX
 *    0133CEA6   . 85D2           TEST EDX,EDX
 *    0133CEA8   . 7F 0A          JG SHORT TinyMach.0133CEB4
 *    0133CEAA   . 8B08           MOV ECX,DWORD PTR DS:[EAX]
 *    0133CEAC   . 8B11           MOV EDX,DWORD PTR DS:[ECX]
 *    0133CEAE   . 50             PUSH EAX
 *    0133CEAF   . 8B42 04        MOV EAX,DWORD PTR DS:[EDX+0x4]
 *    0133CEB2   . FFD0           CALL EAX
 *    0133CEB4   > 8B4C24 14      MOV ECX,DWORD PTR SS:[ESP+0x14]
 *    0133CEB8   . 33DB           XOR EBX,EBX               ; jichi: hook here
 *    0133CEBA   . 3959 F4        CMP DWORD PTR DS:[ECX-0xC],EBX
 *    0133CEBD   . 0F84 D4010000  JE TinyMach.0133D097
 *    0133CEC3   . 8B7424 20      MOV ESI,DWORD PTR SS:[ESP+0x20]
 *    0133CEC7   . E8 F4F90100    CALL TinyMach.0135C8C0     ; jichi: or hook here
 *    0133CECC   . 8BF0           MOV ESI,EAX
 *    0133CECE   . 3BF3           CMP ESI,EBX
 *    0133CED0   . 0F84 C1010000  JE TinyMach.0133D097
 *    0133CED6   . 8D5424 14      LEA EDX,DWORD PTR SS:[ESP+0x14]
 *    0133CEDA   . 52             PUSH EDX                                 ; /Arg2
 *    0133CEDB   . 68 44847D01    PUSH TinyMach.017D8444                   ; |Arg1 = 017D8444
 *    0133CEE0   . E8 EB5BFDFF    CALL TinyMach.01312AD0                   ; \TinyMach.011B2AD0
 *
 *  - Type 3: 剣が君: /HBN-8*0@B357D:KenGaKimi.exe
 *
 *    01113550   . FFF0           PUSH EAX
 *    01113552   . 0FC111         XADD DWORD PTR DS:[ECX],EDX
 *    01113555   . 4A             DEC EDX
 *    01113556   . 85D2           TEST EDX,EDX
 *    01113558   . 7F 0A          JG SHORT KenGaKim.01113564
 *    0111355A   . 8B08           MOV ECX,DWORD PTR DS:[EAX]
 *    0111355C   . 8B11           MOV EDX,DWORD PTR DS:[ECX]
 *    0111355E   . 50             PUSH EAX
 *    0111355F   . 8B42 04        MOV EAX,DWORD PTR DS:[EDX+0x4]
 *    01113562   . FFD0           CALL EAX
 *    01113564     8B4C24 14      MOV ECX,DWORD PTR SS:[ESP+0x14]
 *    01113568     33FF           XOR EDI,EDI
 *    0111356A     3979 F4        CMP DWORD PTR DS:[ECX-0xC],EDI
 *    0111356D     0F84 09020000  JE KenGaKim.0111377C
 *    01113573     8D5424 14      LEA EDX,DWORD PTR SS:[ESP+0x14]
 *    01113577     52             PUSH EDX
 *    01113578     68 DC6A5401    PUSH KenGaKim.01546ADC
 *    0111357D     E8 3EAFF6FF    CALL KenGaKim.0107E4C0    ; hook here
 */
bool FindRejetHook(LPCVOID ins, DWORD ins_size, DWORD ins_off, DWORD hp_off, LPCWSTR hp_name = L"Rejet")
{
  // Offset to the function call from the beginning of the function
  //enum { hook_offset = 0x21 }; // Type1: hex(0x01185332-0x01185311)
  //const BYTE ins[] = {    // Type1: Function start
  //  0xff,0xf0,      // 01185311   . fff0           push eax  ; beginning of a new functino
  //  0x0f,0xc1,0x11, // 01185313   . 0fc111         xadd dword ptr ds:[ecx],edx
  //  0x4a,           // 01185316   . 4a             dec edx
  //  0x85,0xd2,      // 01185317   . 85d2           test edx,edx
  //  0x0f,0x8f       // 01185319   . 0f8f 45020000  jg DotKares.01185564
  //};
  //ITH_GROWL_DWORD(module_base_);
  ULONG addr = module_base_; //- sizeof(ins);
  do {
    //addr += sizeof(ins_size); // ++ so that each time return diff address
    ULONG range = min(module_limit_ - addr, MAX_REL_ADDR);
    ULONG offset = SearchPattern(addr, range, ins, ins_size);
    if (!offset) {
      //ITH_MSG(L"failed");
      ConsoleOutput("vnreng:Rejet: pattern not found");
      return false;
    }

    addr += offset + ins_off;
    //ITH_GROWL_DWORD(addr);
    //ITH_GROWL_DWORD(*(DWORD *)(addr-3));
    //const BYTE hook_ins[] = {
    //  /*0x8b,*/0x74,0x24, 0x20,  //    mov esi,dword ptr ss:[esp+0x20]
    //  0xe8 //??,??,??,??, 01357af9  e8 7283fbff call DotKares.0130fe70 ; jichi: hook here
    //};
  } while(0xe8202474 != *(DWORD *)(addr - 3));

  ConsoleOutput("vnreng: INSERT Rejet");
  HookParam hp = {};
  hp.addr = addr; //- 0xf;
  hp.type = NO_CONTEXT|DATA_INDIRECT;
  hp.length_offset = 1;
  hp.off = hp_off;
  //hp.off = -0x8; // Type1
  //hp.off = -0xc; // Type2

  NewHook(hp, hp_name);
  return true;
}
bool InsertRejetHook1() // This type of hook has varied hook address
{
  const BYTE ins[] = {  // Type1: Function start
    0xff,0xf0,          // 01185311   . fff0           push eax  ; beginning of a new functino
    0x0f,0xc1,0x11,     // 01185313   . 0fc111         xadd dword ptr ds:[ecx],edx
    0x4a,               // 01185316   . 4a             dec edx
    0x85,0xd2,          // 01185317   . 85d2           test edx,edx
    0x0f,0x8f           // 01185319   . 0f8f 45020000  jg DotKares.01185564
  };
  // Offset to the function call from the beginning of the function
  enum { ins_offset = 0x21 }; // Type1: hex(0x01185332-0x01185311)
  enum { hp_off = -0x8 }; // hook parameter
  return FindRejetHook(ins, sizeof(ins), ins_offset, hp_off);
}
bool InsertRejetHook2() // This type of hook has static hook address
{
  const BYTE ins[] = {   // Type2 Function start
    0xff,0xf0,           //   01357ad2   fff0           push eax
    0x0f,0xc1,0x11,      //   01357ad4   0fc111         xadd dword ptr ds:[ecx],edx
    0x4a,                //   01357ad7   4a             dec edx
    0x85,0xd2,           //   01357ad8   85d2           test edx,edx
    0x7f, 0x0a,          //   01357ada   7f 0a          jg short DotKares.01357ae6
    0x8b,0x08,           //   01357adc   8b08           mov ecx,dword ptr ds:[eax]
    0x8b,0x11,           //   01357ade   8b11           mov edx,dword ptr ds:[ecx]
    0x50,                //   01357ae0   50             push eax
    0x8b,0x42, 0x04,     //   01357ae1   8b42 04        mov eax,dword ptr ds:[edx+0x4]
    0xff,0xd0,           //   01357ae4   ffd0           call eax
    0x8b,0x4c,0x24, 0x14 //   01357ae6   8b4c24 14      mov ecx,dword ptr ss:[esp+0x14]
  };
  // Offset to the function call from the beginning of the function
  enum { ins_offset = 0x27 }; // Type2: hex(0x0133CEC7-0x0133CEA0) = hex(0x01357af9-0x1357ad2)
  enum { hp_off = -0xc }; // hook parameter
  return FindRejetHook(ins, sizeof(ins), ins_offset, hp_off);
}
bool InsertRejetHook3() // jichi 12/28/2013: add for 剣が君
{
  // The following pattern is the same as type2
  const BYTE ins[] = {   // Type2 Function start
    0xff,0xf0,           //   01357ad2   fff0           push eax
    0x0f,0xc1,0x11,      //   01357ad4   0fc111         xadd dword ptr ds:[ecx],edx
    0x4a,                //   01357ad7   4a             dec edx
    0x85,0xd2,           //   01357ad8   85d2           test edx,edx
    0x7f, 0x0a,          //   01357ada   7f 0a          jg short DotKares.01357ae6
    0x8b,0x08,           //   01357adc   8b08           mov ecx,dword ptr ds:[eax]
    0x8b,0x11,           //   01357ade   8b11           mov edx,dword ptr ds:[ecx]
    0x50,                //   01357ae0   50             push eax
    0x8b,0x42, 0x04,     //   01357ae1   8b42 04        mov eax,dword ptr ds:[edx+0x4]
    0xff,0xd0,           //   01357ae4   ffd0           call eax
    0x8b,0x4c,0x24, 0x14 //   01357ae6   8b4c24 14      mov ecx,dword ptr ss:[esp+0x14]
  };
  // Offset to the function call from the beginning of the function
  //enum { hook_offset = 0x27 }; // Type2: hex(0x0133CEC7-0x0133CEA0) = hex(0x01357af9-0x1357ad2)
  enum { hp_off = -0xc }; // hook parameter
  ULONG addr = module_base_; //- sizeof(ins);
  while (true) {
    //addr += sizeof(ins_size); // ++ so that each time return diff address
    ULONG range = min(module_limit_ - addr, MAX_REL_ADDR);
    ULONG offset = SearchPattern(addr, range, ins, sizeof(ins));
    if (!offset) {
      //ITH_MSG(L"failed");
      ConsoleOutput("vnreng:Rejet: pattern not found");
      return false;
    }
    addr += offset + sizeof(ins);
    // Push and call at once, i.e. push (0x68) and call (0xe8)
    // 01185345     52             PUSH EDX
    // 01185346   . 68 E4FE5501    PUSH DotKares.0155FEE4                   ; |Arg1 = 0155FEE4
    // 0118534B   . E8 1023F9FF    CALL DotKares.01117660                   ; \DotKares.00377660
    enum { start = 0x10, stop = 0x50 };
    // Different from FindRejetHook
    DWORD i;
    for (i = start; i < stop; i++)
      if (*(WORD *)(addr + i - 1) == 0x6852 && *(BYTE *)(addr + i + 5) == 0xe8) // 0118534B-01185346
        break;
    if (i < stop) {
      addr += i;
      break;
    }
  } //while(0xe8202474 != *(DWORD *)(addr - 3));

  ConsoleOutput("vnreng: INSERT Rejet");
  // The same as type2
  HookParam hp = {};
  hp.addr = addr; //- 0xf;
  hp.type = NO_CONTEXT|DATA_INDIRECT;
  hp.length_offset = 1;
  hp.off = hp_off;
  //hp.off = -0x8; // Type1
  //hp.off = -0xc; // Type2

  NewHook(hp, L"Rejet");
  return true;
}
} // unnamed Rejet

bool InsertRejetHook()
{ return InsertRejetHook2() || InsertRejetHook1() || InsertRejetHook3(); } // insert hook2 first, since 2's pattern seems to be more unique

/**
 *  jichi 1/10/2014: Rai7 puk
 *  See: http://www.hongfire.com/forum/showthread.php/421909-%E3%80%90Space-Warfare-Sim%E3%80%91Rai-7-PUK/page10
 *  See: www.hongfire.com/forum/showthread.php/421909-%E3%80%90Space-Warfare-Sim%E3%80%91Rai-7-PUK/page19
 *
 *  Version: R7P3-13v2(131220).rar, pass: sstm http://pan.baidu.com/share/home?uk=3727185265#category/type=0
 *  /HS0@409524
 */
//bool InsertRai7Hook()
//{
//}

#if 0
/**
 *  jichi 10/1/2013: sol-fa-soft
 *  See (tryguy): http://www.hongfire.com/forum/printthread.php?t=36807&pp=10&page=639
 *
 *  @tryguy
 *  [sol-fa-soft]
 *  17 スク水不要論: /HA4@4AD140
 *  18 ななちゃんといっしょ: /HA4@5104A0
 *  19 発情てんこうせい: /HA4@51D720
 *  20 わたしのたまごさん: /HA4@4968E0
 *  21 修学旅行夜更かし組: /HA4@49DC00
 *  22 おぼえたてキッズ: /HA4@49DDB0
 *  23 ちっさい巫女さんSOS: /HA4@4B4AA0
 *  24 はじめてのおふろやさん: /HA4@4B5600
 *  25 はきわすれ愛好会: /HA4@57E360
 *  26 朝っぱらから発情家族: /HA4@57E360
 *  27 となりのヴァンパイア: /HA4@5593B0
 *  28 麦わら帽子と水辺の妖精: /HA4@5593B0
 *  29 海と温泉と夏休み: /HA4@6DE8E0
 *  30 駄菓子屋さん繁盛記: /HA4@6DEC90
 *  31 浴衣の下は… ～神社で発見！ ノーパン少女～: /HA4@6DEC90
 *  32 プールのじかん スク水不要論2: /HA4@62AE10
 *  33 妹のお泊まり会: /HA4@6087A0
 *  34 薄着少女: /HA4@6087A0
 *  35 あやめ Princess Intermezzo: /HA4@609BF0
 *
 *  SG01 男湯Ｒ: /HA4@6087A0
 *
 *  c71 真冬の大晦日CD: /HA4@516b50
 *  c78 sol-fa-soft真夏のお気楽CD: /HA4@6DEC90
 *
 *  Example: 35 あやめ Princess Intermezzo: /HA4@609BF0
 *  - addr: 6331376 = 0x609bf0
 *  - length_offset: 1
 *  - off: 4
 *  - type: 4
 */
bool InsertSolfaHook()
{
  const BYTE ins[] = {
     // 005f2afd   eb 13          jmp short おぼえた.005f2b12
     0x33,0xc0, // 005f2aff   33c0           xor eax,eax
     0x40, // 005f2b01   40             inc eax
     0xc3, // 005f2b02   c3             retn
     0x8b,0x65, 0xe8 // 005f2b03   8b65 e8        mov esp,dword ptr ss:[ebp-0x18]
     // 005f2b06   c745 fc feffff>mov dword ptr ss:[ebp-0x4],-0x2
     // 005f2b0d   b8 ff000000    mov eax,0xff
     // 005f2b12   e8 865a0000    call おぼえた.005f859d
     // 005f2b17   c3             retn
     // 005f2b18   e8 395c0000    call おぼえた.005f8756
  };
  ULONG range = min(module_limit_ - module_base_, MAX_REL_ADDR);
  ULONG reladdr = SearchPattern(module_base_, range, ins, sizeof(ins));
  //ITH_GROWL_DWORD4(reladdr, module_base_, range, module_base_ + reladdr);
  if (!reladdr) {
    ConsoleOutput("vnreng:SolfaSoft: pattern not found");
    return false;
  }
  const BYTE ins2[] = {
    0xc3,   // retn
    0xe8    // call XXXX
  };
  ULONG cur = module_base_ + reladdr + sizeof(ins2);
  reladdr = SearchPattern(cur, 0x100, ins2, sizeof(ins2));
  //ITH_GROWL_DWORD3(reladdr, base, base + reladdr);
  if (!reladdr)
    return false;
  cur += reladdr + sizeof(ins2);
  DWORD func = *(DWORD *)cur;
  ITH_GROWL_DWORD2(cur, func);
  // CHECKPOINT: the hoooked address is where this func is being invoked
  //return false;

  HookParam hp = {};
  hp.addr = module_base_ + reladdr;
  hp.length_offset = 1;
  hp.off = 4;
  hp.type = BIG_ENDIAN; // 4

  //hp.addr = 0x609bf0;
  //ITH_GROWL_DWORD(hp.addr);

  ConsoleOutput("vnreng: INSERT SolfaSoft");
  NewHook(hp, L"SolfaSoft");
  return true;
}
#endif // 0

#if 0

/**
 *  jichi 10/3/2013: BALDRSKY ZERO  (Unity3D)
 *  See (ok123): http://9gal.com/read.php?tid=411756
 *  Pattern: 90FF503C83C4208B45EC
 *
 *
 *  Example: /HQN4@7620DA1 (or /HQN84:84*-C@1005FFCB)
 *  - addr: 123866529 = 0x7620da1
 *  - off: 4
 *  - type: 1027 = 0x403
 *
 *  FIXME: Raise C0000005 even with admin priv
 */
bool InsertBaldrHook()
{
  // Instruction pattern: 90FF503C83C4208B45EC
  cons BYTE ins[] = {0x90,0xff,0x50,0x3c,0x83,0xc4,0x20,0x8b,0x45,0xec};
  //const BYTE ins[] = {0xec,0x45,0x8b,0x20,0xc4,0x83,0x3c,0x50,0xff,0x90};
  enum { hook_offset = 0 };
  enum { limit = 0x10000000 }; // very large ><
  //enum { range = 0x10000000 };
  //enum { range = 0x1000000 };
  //enum { range = 0x7fffffff };
  //ULONG range = min(module_limit_ - module_base_, MAX_REL_ADDR);
  ULONG range = min(module_limit_ - module_base_, limit);
  ULONG reladdr = SearchPattern(module_base_, range, ins, sizeof(ins));
  //ITH_GROWL_DWORD3(base, range, reladdr);
  if (!reladdr) {
    ConsoleOutput("vnreng:BALDR: pattern not found");
    return false;
  }

  HookParam hp = {};
  hp.addr = module_base_ + reladdr + hook_offset;
  hp.off = 4;
  hp.type = NO_CONTEXT | USING_STRING | USING_UNICODE; // 0x403

  //hp.addr = 0x650a2f;
  //ITH_GROWL_DWORD(hp.addr);

  ConsoleOutput("vnreng: INSERT BALDR");
  NewHook(hp, L"BALDR");
  //ConsoleOutput("Artemis");
  return true;
}

#endif // 0

} // namespace Engine

// EOF
