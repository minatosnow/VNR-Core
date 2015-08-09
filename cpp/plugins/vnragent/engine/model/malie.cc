// malie.cc
// 8/8/2015 jichi
#include "engine/model/malie.h"
#include "engine/enginecontroller.h"
#include "engine/enginedef.h"
#include "engine/enginehash.h"
#include "engine/engineutil.h"
//#include "hijack/hijackmanager.h"
#include "util/textutil.h"
#include "winhook/hookcode.h"
#include "memdbg/memsearch.h"
#include <qt_windows.h>
#include <cstdint>
#include <unordered_set>

#define DEBUG "model/malie"
#include "sakurakit/skdebug.h"

namespace { // unnamed
namespace ScenarioHook {
namespace Private {

  /**
   *  0706: pause, text separator
   *  0708: voice start.
   *  0701: ruby start, 0a as separator
   *
   *  Sample plain unvoiced text:
   *
   *  0706 is used as pause char.
   *
   *  01FFF184  00 30 2A 8A 8C 30 8B 30 21 6B 6E 30 27 59 75 65  　訪れる次の大敵
   *  01FFF194  00 25 00 25 21 6B 6E 30 0D 4E 78 5E 02 30 21 6B  ──次の不幸。次
   *  01FFF1A4  6E 30 E6 82 E3 96 02 30 21 6B 6E 30 34 78 C5 6E  の苦難。次の破滅
   *  01FFF1B4  02 30 07 00 06 00 0A 00 00 30 B4 63 7F 30 D6 53  。.　掴み取
   *  01FFF1C4  63 30 5F 30 6F 30 5A 30 6E 30 2A 67 65 67 6F 30  ったはずの未来は
   *  01FFF1D4  97 66 D2 9E 6B 30 55 87 7E 30 8C 30 5F 30 7E 30  暗黒に蝕まれたま
   *  01FFF1E4  7E 30 9A 7D 4C 88 57 30 66 30 44 30 4F 30 02 30  ま続行していく。
   *  01FFF1F4  07 00 06 00 0A 00 00 30 80 30 57 30 8D 30 4B 62  .　むしろ手
   *  01FFF204  6B 30 57 30 5F 30 47 59 E1 8D 92 30 7C 54 73 30  にした奇跡を呼び
   *  01FFF214  34 6C 6B 30 01 30 88 30 8A 30 4A 30 5E 30 7E 30  水に、よりおぞま
   *  01FFF224  57 30 44 30 B0 65 5F 30 6A 30 66 8A F4 7D 92 30  しい新たな試練を
   *  01FFF234  44 7D 7F 30 BC 8F 93 30 67 30 4B 90 7D 54 92 30  組み込んで運命を
   *  01FFF244  C6 99 D5 52 55 30 5B 30 8B 30 6E 30 60 30 02 30  駆動させるのだ。
   *  01FFF254  07 00 06 00 00 00 00 00 00 00 00 00 00 00 00 00  ......
   *  01FFF264  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ........
   *  01FFF274  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ........
   *  01FFF284  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ........
   *  01FFF294  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ........
   *  01FFF2A4  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ........
   *
   *  Mixed unvoiced text and voiced text list:
   *  01FFF184  00 30 1C 20 DD 52 29 52 1D 20 4B 30 89 30 6F 30  　“勝利”からは
   *  01FFF194  03 90 52 30 89 30 8C 30 6A 30 44 30 02 30 07 00  逃げられない。
   *  01FFF1A4  06 00 0A 00 00 30 1C 20 DD 52 29 52 1D 20 4B 30  .　“勝利”か
   *  01FFF1B4  89 30 6F 30 03 90 52 30 89 30 8C 30 6A 30 44 30  らは逃げられない
   *  01FFF1C4  02 30 07 00 06 00 0A 00 00 30 1C 20 DD 52 29 52  。.　“勝利
   *  01FFF1D4  1D 20 4B 30 89 30 6F 30 03 90 52 30 89 30 8C 30  ”からは逃げられ
   *  01FFF1E4  6A 30 44 30 02 30 07 00 06 00 0A 00 0A 00 07 00  ない。..
   *  01FFF1F4  08 00 76 00 5F 00 76 00 6E 00 64 00 30 00 30 00  v_vnd00
   *  01FFF204  30 00 31 00 00 00 0C 30 6A 30 89 30 70 30 00 25  01.「ならば─
   *  01FFF214  00 25 00 25 00 25 0D 30 07 00 09 00 07 00 06 00  ───」.
   *  01FFF224  0A 00 0A 00 00 30 00 25 00 25 55 30 42 30 01 30  ..　──さあ、
   *  01FFF234  69 30 46 30 59 30 8B 30 4B 30 1F FF 07 00 06 00  どうするか？
   *  01FFF244  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ........
   *  01FFF254  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ........
   *  01FFF264  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ........
   *
   *  Sample voiced text:
   *
   *  0269F184  07 00 08 00 76 00 5F 00 7A 00 65 00 70 00 30 00  v_zep0
   *  0269F194  30 00 30 00 31 00 00 00 1C 20 DD 52 29 52 1D 20  001.“勝利”
   *  0269F1A4  68 30 6F 30 01 30 55 4F 60 30 1F FF 07 00 09 00  とは、何だ？.
   *  0269F1B4  07 00 06 00 0A 00 0A 00 07 00 08 00 76 00 5F 00  ..v_
   *  0269F1C4  7A 00 65 00 70 00 30 00 30 00 30 00 32 00 00 00  zep0002.
   *  0269F1D4  1C 20 04 68 49 51 1D 20 68 30 6F 30 01 30 55 4F  “栄光”とは、何
   *  0269F1E4  60 30 1F FF 07 00 09 00 07 00 06 00 0A 00 0A 00  だ？...
   *  0269F1F4  07 00 08 00 76 00 5F 00 7A 00 65 00 70 00 30 00  v_zep0
   *  0269F204  30 00 30 00 33 00 00 00 5D 30 8C 30 92 30 97 5F  003.それを得
   *  0269F214  8C 30 70 30 01 30 55 4F 82 30 31 59 8F 30 5A 30  れば、何も失わず
   *  0269F224  6B 30 08 6E 80 30 6E 30 60 30 8D 30 46 30 4B 30  に済むのだろうか
   *  0269F234  07 00 09 00 07 00 06 00 0A 00 0A 00 07 00 08 00  ...
   *  0269F244  76 00 5F 00 7A 00 65 00 70 00 30 00 30 00 30 00  v_zep000
   *  0269F254  34 00 00 00 51 65 48 30 8B 30 6E 30 4B 30 02 30  4.救えるのか。
   *  0269F264  88 5B 8C 30 8B 30 6E 30 4B 30 02 30 2C 67 53 5F  守れるのか。本当
   *  0269F274  6B 30 01 30 78 5E 5B 30 6B 30 6A 30 8C 30 8B 30  に、幸せになれる
   *  0269F284  6E 30 60 30 8D 30 46 30 4B 30 07 00 09 00 07 00  のだろうか.
   *  0269F294  06 00 00 00 00 00 00 00 D1 01 00 00 8C F3 69 02  ...Ǒ.ɩ
   *
   *  Ruby:
   *
   *  01FDF2B4  63 30 5F 30 07 00 01 00 14 90 EF 7A 0A 00 68 30  った途端.と
   *  01FDF2C4  5F 30 93 30 00 00 01 30 06 90 6B 30 40 62 09 67  たん.、逆に所有
   *
   *  Pause without 0a:
   *
   *  0271F184  07 00 08 00 76 00 5F 00 7A 00 65 00 70 00 30 00  v_zep0
   *  0271F194  30 00 34 00 34 00 00 00 00 30 51 30 8C 30 69 30  044.　けれど
   *  0271F1A4  00 25 00 25 07 00 09 00 07 00 06 00 07 00 08 00  ──.
   *  0271F1B4  76 00 5F 00 7A 00 65 00 70 00 30 00 30 00 34 00  v_zep004
   *  0271F1C4  35 00 00 00 5D 30 8C 30 67 30 82 30 01 30 88 5B  5.それでも、守
   *  0271F1D4  89 30 6A 30 51 30 8C 30 70 30 6A 30 89 30 6A 30  らなければならな
   *  0271F1E4  44 30 50 5B 4C 30 FA 51 65 67 5F 30 4B 30 89 30  い子が出来たから
   *  0271F1F4  02 30 07 00 09 00 07 00 06 00 07 00 04 00 00 30  。.　
   *  0271F204  07 00 08 00 76 00 5F 00 7A 00 65 00 70 00 30 00  v_zep0
   *  0271F214  30 00 34 00 36 00 00 00 7C 5F 73 59 92 30 51 65  046.彼女を救
   *  0271F224  46 30 5F 30 81 30 6B 30 01 30 53 30 6E 30 61 30  うために、このち
   *  0271F234  63 30 7D 30 51 30 6A 30 7D 54 92 30 F8 61 51 30  っぽけな命を懸け
   *  0271F244  8B 30 68 30 93 8A 63 30 5F 30 02 30 86 30 48 30  ると誓った。ゆえ
   *
   *  Scenario caller: 4637bf
   *
   *  0046377D   90               NOP
   *  0046377E   90               NOP
   *  0046377F   90               NOP
   *  00463780   81EC 00080000    SUB ESP,0x800
   *  00463786   56               PUSH ESI
   *  00463787   8BB424 08080000  MOV ESI,DWORD PTR SS:[ESP+0x808]
   *  0046378E   8B46 1C          MOV EAX,DWORD PTR DS:[ESI+0x1C]
   *  00463791   8B88 68020000    MOV ECX,DWORD PTR DS:[EAX+0x268]
   *  00463797   57               PUSH EDI
   *  00463798   51               PUSH ECX
   *  00463799   E8 D200FFFF      CALL malie.00453870
   *  0046379E   8BBC24 14080000  MOV EDI,DWORD PTR SS:[ESP+0x814]
   *  004637A5   68 C06C4100      PUSH malie.00416CC0
   *  004637AA   8D5424 10        LEA EDX,DWORD PTR SS:[ESP+0x10]
   *  004637AE   57               PUSH EDI
   *  004637AF   52               PUSH EDX
   *  004637B0   E8 AB041F00      CALL malie.00653C60
   *  004637B5   8D4424 18        LEA EAX,DWORD PTR SS:[ESP+0x18]
   *  004637B9   50               PUSH EAX
   *  004637BA   E8 21031F00      CALL malie.00653AE0   ; jichi: scenario caller
   *  004637BF   8B4E 1C          MOV ECX,DWORD PTR DS:[ESI+0x1C]
   *  004637C2   57               PUSH EDI
   *  004637C3   8981 68020000    MOV DWORD PTR DS:[ECX+0x268],EAX
   *  004637C9   E8 32E61E00      CALL malie.00651E00
   *  004637CE   83C4 18          ADD ESP,0x18
   *  004637D1   33D2             XOR EDX,EDX
   *  004637D3   85C0             TEST EAX,EAX
   *  004637D5   8B46 1C          MOV EAX,DWORD PTR DS:[ESI+0x1C]
   *  004637D8   0F9FC2           SETG DL
   *  004637DB   5F               POP EDI
   *  004637DC   5E               POP ESI
   *  004637DD   8990 7C020000    MOV DWORD PTR DS:[EAX+0x27C],EDX
   *  004637E3   81C4 00080000    ADD ESP,0x800
   *  004637E9   C3               RETN
   *  004637EA   90               NOP
   *  004637EB   90               NOP
   *  004637EC   90               NOP
   *
   *  Name caller: 46382e
   *
   * 004637EB   90               NOP
   * 004637EC   90               NOP
   * 004637ED   90               NOP
   * 004637EE   90               NOP
   * 004637EF   90               NOP
   * 004637F0   81EC 00080000    SUB ESP,0x800
   * 004637F6   56               PUSH ESI
   * 004637F7   8BB424 08080000  MOV ESI,DWORD PTR SS:[ESP+0x808]
   * 004637FE   8B46 1C          MOV EAX,DWORD PTR DS:[ESI+0x1C]
   * 00463801   8B88 6C020000    MOV ECX,DWORD PTR DS:[EAX+0x26C]
   * 00463807   51               PUSH ECX
   * 00463808   E8 6300FFFF      CALL malie.00453870
   * 0046380D   8B9424 10080000  MOV EDX,DWORD PTR SS:[ESP+0x810]
   * 00463814   68 C06C4100      PUSH malie.00416CC0
   * 00463819   52               PUSH EDX
   * 0046381A   8D4424 10        LEA EAX,DWORD PTR SS:[ESP+0x10]
   * 0046381E   50               PUSH EAX
   * 0046381F   E8 3C041F00      CALL malie.00653C60
   * 00463824   8D4C24 14        LEA ECX,DWORD PTR SS:[ESP+0x14]
   * 00463828   51               PUSH ECX
   * 00463829   E8 B2021F00      CALL malie.00653AE0 ; jichi: name
   * 0046382E   8B56 1C          MOV EDX,DWORD PTR DS:[ESI+0x1C]
   * 00463831   83C4 14          ADD ESP,0x14
   * 00463834   8982 6C020000    MOV DWORD PTR DS:[EDX+0x26C],EAX
   * 0046383A   5E               POP ESI
   * 0046383B   81C4 00080000    ADD ESP,0x800
   * 00463841   C3               RETN
   * 00463842   90               NOP
   * 00463843   90               NOP
   * 00463844   90               NOP
   *
   *  History caller: 418d0b
   *
   *  00418C9D   90               NOP
   *  00418C9E   90               NOP
   *  00418C9F   90               NOP
   *  00418CA0   81EC 00080000    SUB ESP,0x800
   *  00418CA6   53               PUSH EBX
   *  00418CA7   56               PUSH ESI
   *  00418CA8   57               PUSH EDI
   *  00418CA9   6A 6C            PUSH 0x6C
   *  00418CAB   FF15 20256900    CALL DWORD PTR DS:[<&MSVCRT.malloc>]     ; msvcrt.malloc
   *  00418CB1   8BD8             MOV EBX,EAX
   *  00418CB3   83C4 04          ADD ESP,0x4
   *  00418CB6   85DB             TEST EBX,EBX
   *  00418CB8   0F84 D1000000    JE malie.00418D8F
   *  00418CBE   8BB424 10080000  MOV ESI,DWORD PTR SS:[ESP+0x810]
   *  00418CC5   33C0             XOR EAX,EAX
   *  00418CC7   B9 1B000000      MOV ECX,0x1B
   *  00418CCC   8BFB             MOV EDI,EBX
   *  00418CCE   F3:AB            REP STOS DWORD PTR ES:[EDI]
   *  00418CD0   8B06             MOV EAX,DWORD PTR DS:[ESI]
   *  00418CD2   68 C06C4100      PUSH malie.00416CC0
   *  00418CD7   50               PUSH EAX
   *  00418CD8   8D4C24 14        LEA ECX,DWORD PTR SS:[ESP+0x14]
   *  00418CDC   51               PUSH ECX
   *  00418CDD   E8 7EAF2300      CALL malie.00653C60
   *  00418CE2   8D5424 18        LEA EDX,DWORD PTR SS:[ESP+0x18]
   *  00418CE6   52               PUSH EDX
   *  00418CE7   E8 F4AD2300      CALL malie.00653AE0
   *  00418CEC   8903             MOV DWORD PTR DS:[EBX],EAX
   *  00418CEE   8B46 04          MOV EAX,DWORD PTR DS:[ESI+0x4]
   *  00418CF1   68 C06C4100      PUSH malie.00416CC0
   *  00418CF6   50               PUSH EAX
   *  00418CF7   8D4C24 24        LEA ECX,DWORD PTR SS:[ESP+0x24]
   *  00418CFB   51               PUSH ECX
   *  00418CFC   E8 5FAF2300      CALL malie.00653C60
   *  00418D01   8D5424 28        LEA EDX,DWORD PTR SS:[ESP+0x28]
   *  00418D05   52               PUSH EDX
   *  00418D06   E8 D5AD2300      CALL malie.00653AE0 ; jichi: history caller
   *  00418D0B   8943 04          MOV DWORD PTR DS:[EBX+0x4],EAX
   *  00418D0E   8B46 08          MOV EAX,DWORD PTR DS:[ESI+0x8]
   *  00418D11   83C4 20          ADD ESP,0x20
   *  00418D14   85C0             TEST EAX,EAX
   *  00418D16   75 05            JNZ SHORT malie.00418D1D
   *  00418D18   B8 0CEF7000      MOV EAX,malie.0070EF0C
   *  00418D1D   50               PUSH EAX
   *  00418D1E   E8 3D6F2300      CALL malie.0064FC60
   *  00418D23   8943 08          MOV DWORD PTR DS:[EBX+0x8],EAX
   *  00418D26   8B46 0C          MOV EAX,DWORD PTR DS:[ESI+0xC]
   *  00418D29   83C4 04          ADD ESP,0x4
   *  00418D2C   85C0             TEST EAX,EAX
   *  00418D2E   75 05            JNZ SHORT malie.00418D35
   *  00418D30   B8 0CEF7000      MOV EAX,malie.0070EF0C
   *  00418D35   50               PUSH EAX
   *  00418D36   E8 256F2300      CALL malie.0064FC60
   *  00418D3B   8943 0C          MOV DWORD PTR DS:[EBX+0xC],EAX
   *  00418D3E   8B46 60          MOV EAX,DWORD PTR DS:[ESI+0x60]
   *  00418D41   8943 60          MOV DWORD PTR DS:[EBX+0x60],EAX
   *  00418D44   8B4E 64          MOV ECX,DWORD PTR DS:[ESI+0x64]
   *  00418D47   894B 64          MOV DWORD PTR DS:[EBX+0x64],ECX
   *  00418D4A   8B56 68          MOV EDX,DWORD PTR DS:[ESI+0x68]
   *  00418D4D   8D7E 10          LEA EDI,DWORD PTR DS:[ESI+0x10]
   *  00418D50   83C4 04          ADD ESP,0x4
   *  00418D53   85FF             TEST EDI,EDI
   *  00418D55   8953 68          MOV DWORD PTR DS:[EBX+0x68],EDX
   *  00418D58   74 35            JE SHORT malie.00418D8F
   *  00418D5A   55               PUSH EBP
   *  00418D5B   8BEB             MOV EBP,EBX
   *  00418D5D   2BEE             SUB EBP,ESI
   *  00418D5F   BE 14000000      MOV ESI,0x14
   *  00418D64   8B07             MOV EAX,DWORD PTR DS:[EDI]
   *  00418D66   66:8338 00       CMP WORD PTR DS:[EAX],0x0
   *  00418D6A   75 04            JNZ SHORT malie.00418D70
   *  00418D6C   33C0             XOR EAX,EAX
   *  00418D6E   EB 09            JMP SHORT malie.00418D79
   *  00418D70   50               PUSH EAX
   *  00418D71   E8 EA6E2300      CALL malie.0064FC60
   *  00418D76   83C4 04          ADD ESP,0x4
   *  00418D79   89042F           MOV DWORD PTR DS:[EDI+EBP],EAX
   *  00418D7C   83C7 04          ADD EDI,0x4
   *  00418D7F   4E               DEC ESI
   *  00418D80  ^75 E2            JNZ SHORT malie.00418D64
   *  00418D82   5D               POP EBP
   *  00418D83   5F               POP EDI
   *  00418D84   5E               POP ESI
   *  00418D85   8BC3             MOV EAX,EBX
   *  00418D87   5B               POP EBX
   *  00418D88   81C4 00080000    ADD ESP,0x800
   *  00418D8E   C3               RETN
   *  00418D8F   5F               POP EDI
   *  00418D90   5E               POP ESI
   *  00418D91   8BC3             MOV EAX,EBX
   *  00418D93   5B               POP EBX
   *  00418D94   81C4 00080000    ADD ESP,0x800
   *  00418D9A   C3               RETN
   *  00418D9B   90               NOP
   *  00418D9C   90               NOP
   *
   *  Exit dialog box caller:
   *  00475A8D   90               NOP
   *  00475A8E   90               NOP
   *  00475A8F   90               NOP
   *  00475A90   56               PUSH ESI
   *  00475A91   68 B09C7500      PUSH malie.00759CB0
   *  00475A96   FF15 F8206900    CALL DWORD PTR DS:[<&KERNEL32.EnterCriti>; ntdll.RtlEnterCriticalSection
   *  00475A9C   8B7424 08        MOV ESI,DWORD PTR SS:[ESP+0x8]
   *  00475AA0   85F6             TEST ESI,ESI
   *  00475AA2   74 4A            JE SHORT malie.00475AEE
   *  00475AA4   56               PUSH ESI
   *  00475AA5   E8 56000000      CALL malie.00475B00
   *  00475AAA   8B46 1C          MOV EAX,DWORD PTR DS:[ESI+0x1C]
   *  00475AAD   8B08             MOV ECX,DWORD PTR DS:[EAX]
   *  00475AAF   51               PUSH ECX
   *  00475AB0   E8 BBDDFDFF      CALL malie.00453870
   *  00475AB5   8B5424 14        MOV EDX,DWORD PTR SS:[ESP+0x14]
   *  00475AB9   52               PUSH EDX
   *  00475ABA   E8 21E01D00      CALL malie.00653AE0 ; jichi: called here
   *  00475ABF   8B4E 1C          MOV ECX,DWORD PTR DS:[ESI+0x1C]
   *  00475AC2   8901             MOV DWORD PTR DS:[ECX],EAX
   *  00475AC4   8B56 1C          MOV EDX,DWORD PTR DS:[ESI+0x1C]
   *  00475AC7   C782 94000000 00>MOV DWORD PTR DS:[EDX+0x94],0x0
   *  00475AD1   8B46 1C          MOV EAX,DWORD PTR DS:[ESI+0x1C]
   *  00475AD4   8B08             MOV ECX,DWORD PTR DS:[EAX]
   *  00475AD6   51               PUSH ECX
   *  00475AD7   E8 84C41D00      CALL malie.00651F60
   *  00475ADC   8B56 1C          MOV EDX,DWORD PTR DS:[ESI+0x1C]
   *  00475ADF   56               PUSH ESI
   *  00475AE0   8982 98000000    MOV DWORD PTR DS:[EDX+0x98],EAX
   *  00475AE6   E8 C5000000      CALL malie.00475BB0
   *  00475AEB   83C4 14          ADD ESP,0x14
   *  00475AEE   68 B09C7500      PUSH malie.00759CB0
   *  00475AF3   FF15 44226900    CALL DWORD PTR DS:[<&KERNEL32.LeaveCriti>; ntdll.RtlLeaveCriticalSection
   *  00475AF9   5E               POP ESI
   *  00475AFA   C3               RETN
   *  00475AFB   90               NOP
   *  00475AFC   90               NOP
   *  00475AFD   90               NOP
   */

  uint64_t hashTextList(LPCWSTR text)
  {
    bool voiced = text[0] == 0x7 && text[1] == 0x8;
    auto h = Engine::hashWCharArray(text); // hash scenario ID
    text += ::wcslen(text) + 1;
    if (voiced && *text)
      h = Engine::hashWCharArray(text, ::wcslen(text), h); // only the first text is hashed
    return h;
  }

  size_t parseTextSize(LPCWSTR text)
  {
    size_t count = 0;
    bool skipNull = false;
    for (; *text || skipNull; text++, count++)
      if (text[0] == 0)
        skipNull = false;
      else if (text[0] == 0x7)
        switch (text[1]) {
        case 0x1:  // ruby
          skipNull = true;
          break;
        case 0x8: // voice
          return count;
        case 0x6: // pause
          return count + 2;
        }
    return count;
  }

  size_t rtrim(LPCWSTR text, size_t size)
  {
    while (size && (text[size - 1] <= 32 || text[size - 1] == 0x3000)) // trim trailing non-printable characters
      size--;
    return size;
  }

  QByteArray parseTextData(LPCWSTR text)
  {
    QByteArray ret;
    if (!wcschr(text, 0x7)) {
      ret.setRawData((LPCSTR)text, ::wcslen(text) * sizeof(wchar_t));
      return ret;
    }
    for (; *text; text++) {
      if (text[0] == 0x7)
        switch (text[1]) {
        case 0x1:  // ruby
          if (LPCWSTR p = ::wcschr(text + 2, 0xa)) {
            ret.append(LPCSTR(text + 2), (p - text - 2) * sizeof(wchar_t));
            text = p + ::wcslen(p); // text now point to zero
            continue;
          } // mismatched ruby that should never happen
          return QByteArray();
        case 0x8: // voice
          return ret;
        case 0x6: // pause
          ret.append((LPCSTR)text, 2 * sizeof(wchar_t));
          return ret;
        }
      ret.append((LPCSTR)text, sizeof(wchar_t));
    }
    return ret;
  }

  // I need a cache retainer here to make sure same text result in same result
  bool hookBefore(winhook::hook_stack *s)
  {
    static QByteArray data_;
    static std::unordered_set<uint64_t> hashes_;
    auto text = (LPCWSTR)s->stack[1];
    if (!text || !*text
        //|| text[0] != 0x7 || text[1] != 0x8
        || hashes_.find(hashTextList(text)) !=  hashes_.end())
      return true;

    // Scenario caller:
    // 004637BA   E8 21031F00      CALL malie.00653AE0   ; jichi: scenario caller
    // 004637BF   8B4E 1C          MOV ECX,DWORD PTR DS:[ESI+0x1C]
    // 004637C2   57               PUSH EDI
    //
    // Name caller:
    // 00463829   E8 B2021F00      CALL malie.00653AE0 ; jichi: name
    // 0046382E   8B56 1C          MOV EDX,DWORD PTR DS:[ESI+0x1C]
    // 00463831   83C4 14          ADD ESP,0x14
    auto role = Engine::OtherRole;
    auto retaddr = s->stack[0];
    switch (*(DWORD *)retaddr) {
    case 0x571c4e8b: role = Engine::ScenarioRole; break;
    case 0x831c568b: role = Engine::NameRole; break;
    }
    //auto sig = Engine::hashThreadSignature(role, retaddr); // this is not needed as the retaddr is used as split
    auto sig = retaddr;

    QByteArray data;
    bool update = false;

    for (size_t size; *text; text += size) {
      if (text[0] == 0x7 && text[1] == 0x8) { // voiced
        size_t len = ::wcslen(text);
        data.append((LPCSTR)text, (len + 1) * sizeof(wchar_t));
        text += len + 1;
      }

      size = parseTextSize(text);
      QByteArray oldData = parseTextData(text);
      if (oldData.isEmpty()) // this should never happen
        return true;

      auto oldTextAddress = (LPCWSTR)oldData.constData();
      size_t oldTextSize = oldData.size() / sizeof(wchar_t),
             trimmedSize = rtrim(oldTextAddress, oldTextSize);
      if (trimmedSize == 0 || Util::allAscii(oldTextAddress, trimmedSize))
        data.append(oldData);
      else {
        QString oldText = QString::fromWCharArray(oldTextAddress, trimmedSize),
                newText = EngineController::instance()->dispatchTextW(oldText, role, sig);
        if (newText.isEmpty() || newText == oldText)
          data.append(oldData);
        else {
          update = true;
          data.append((LPCSTR)newText.utf16(), newText.size() * sizeof(wchar_t));
          if (trimmedSize != oldTextSize)
            data.append(LPCSTR(oldTextAddress + trimmedSize), (oldTextSize - trimmedSize) * sizeof(wchar_t));
        }
      }
    }
    if (update) {
      {
        static const QByteArray zero_bytes(sizeof(wchar_t), '\0'),
                                zero_repr((LPCSTR)MALIE_0, sizeof(MALIE_0) - sizeof(wchar_t)); // - \0's size
        data.replace(zero_repr, zero_bytes);
      }

      // make sure there are 4 zeros at the end
      data.append('\0')
          .append('\0')
          .append('\0');
      data_ = data;
      text = (LPCWSTR)data_.constData();
      hashes_.insert(hashTextList(text)); // only the first text is hashed
      s->stack[1] = (ulong)text;
    }
    return true;
  }

} // namespace Private

/**
 *  Sample game: シルヴァリオ ヴェンデッタ
 *
 *  Text in arg1.
 *  Function found by debugging the text being accessed.
 *  It is the same as one of the parent call of Malie2.
 *
 *  The target text arg1 is on this function's caller's stack.
 *
 *  00653ADC   90               NOP
 *  00653ADD   90               NOP
 *  00653ADE   90               NOP
 *  00653ADF   90               NOP
 *  00653AE0   56               PUSH ESI
 *  00653AE1   8B7424 08        MOV ESI,DWORD PTR SS:[ESP+0x8]
 *  00653AE5   33C0             XOR EAX,EAX
 *  00653AE7   85F6             TEST ESI,ESI
 *  00653AE9   74 47            JE SHORT malie.00653B32
 *  00653AEB   53               PUSH EBX
 *  00653AEC   57               PUSH EDI
 *  00653AED   68 00C47F00      PUSH malie.007FC400
 *  00653AF2   FF15 F8206900    CALL DWORD PTR DS:[<&KERNEL32.EnterCriti>; ntdll.RtlEnterCriticalSection
 *  00653AF8   56               PUSH ESI
 *  00653AF9   E8 C2E4FFFF      CALL malie.00651FC0
 *  00653AFE   8D78 02          LEA EDI,DWORD PTR DS:[EAX+0x2]
 *  00653B01   57               PUSH EDI
 *  00653B02   FF15 20256900    CALL DWORD PTR DS:[<&MSVCRT.malloc>]     ; msvcrt.malloc
 *  00653B08   8BD8             MOV EBX,EAX
 *  00653B0A   83C4 08          ADD ESP,0x8
 *  00653B0D   85DB             TEST EBX,EBX
 *  00653B0F   74 12            JE SHORT malie.00653B23
 *  00653B11   8BCF             MOV ECX,EDI
 *  00653B13   8BFB             MOV EDI,EBX
 *  00653B15   8BC1             MOV EAX,ECX
 *  00653B17   C1E9 02          SHR ECX,0x2
 *  00653B1A   F3:A5            REP MOVS DWORD PTR ES:[EDI],DWORD PTR DS>
 *  00653B1C   8BC8             MOV ECX,EAX
 *  00653B1E   83E1 03          AND ECX,0x3
 *  00653B21   F3:A4            REP MOVS BYTE PTR ES:[EDI],BYTE PTR DS:[>
 *  00653B23   68 00C47F00      PUSH malie.007FC400
 *  00653B28   FF15 44226900    CALL DWORD PTR DS:[<&KERNEL32.LeaveCriti>; ntdll.RtlLeaveCriticalSection
 *  00653B2E   8BC3             MOV EAX,EBX
 *  00653B30   5F               POP EDI
 *  00653B31   5B               POP EBX
 *  00653B32   5E               POP ESI
 *  00653B33   C3               RETN
 *  00653B34   90               NOP
 *  00653B35   90               NOP
 *  00653B36   90               NOP
 *  00653B37   90               NOP
 *  00653B38   90               NOP
 */
bool attach(ulong startAddress, ulong stopAddress)
{
  const uint8_t bytes[] = {
    0x8b,0xd8,          // 00653b08   8bd8             mov ebx,eax
    0x83,0xc4, 0x08,    // 00653b0a   83c4 08          add esp,0x8
    0x85,0xdb,          // 00653b0d   85db             test ebx,ebx
    0x74, 0x12,         // 00653b0f   74 12            je short malie.00653b23
    0x8b,0xcf,          // 00653b11   8bcf             mov ecx,edi
    0x8b,0xfb,          // 00653b13   8bfb             mov edi,ebx
    0x8b,0xc1,          // 00653b15   8bc1             mov eax,ecx
    0xc1,0xe9, 0x02     // 00653b17   c1e9 02          shr ecx,0x2
  };
  ulong addr = MemDbg::findBytes(bytes, sizeof(bytes), startAddress, stopAddress);
  if (!addr)
    return false;
  addr = MemDbg::findEnclosingAlignedFunction(addr);
  if (!addr)
    return false;
  //addr = 0x00653AE0; // the actual hooked grant parent call function, text in arg1

  // Sample game: シルヴァリオ ヴェンデッタ
  // If there are untranslated function, hook to the following location and debug the function stack to find text address
  //addr = 0x006519B0; // the callee function, text in arg2, function called by two functions, including the callee. Hooking to this function causing history to crash
  return winhook::hook_before(addr, Private::hookBefore);
}
} // namespace ScenarioHook
} // unnamed namespace

bool MalieEngine::attach()
{
  ulong startAddress, stopAddress;
  if (!Engine::getProcessMemoryRange(&startAddress, &stopAddress))
    return false;
  if (!ScenarioHook::attach(startAddress, stopAddress))
    return false;
  // FIXME: Font cannot be changed at runtime anyway
  // I might have to hook to GDI+ font functions instead of GDI to make it work.
  //HijackManager::instance()->attachFunction((ulong)::GetGlyphOutlineW);
  //HijackManager::instance()->attachFunction((ulong)::CreateFontIndirectW);
  return true;
}

QString MalieEngine::rubyCreate(const QString &rb, const QString &rt)
{
  static QString fmt = QString::fromWCharArray(L"\x07\x01%1\x0a%2" MALIE_0);
  return fmt.arg(rb, rt);
}

// EOF
