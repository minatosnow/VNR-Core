// myfunc.cc
// 1/27/2013 jichi
// TODO: Split the code for text detection and text modification
//
// Detours VS InjectFunction
// - Detours cannot limit the overridden within a specific module
// - Detours has thread-safe lock
// - InjectFunction cannot override executable module?
//
#include "winhook/myfunc.h"
#include "winhook/myfunc_p.h"
#include "winhook/qt/mainobj.h"
#include "winhook/util/winsec.h"
#include <psapi.h>
//#include <detours.h>
#include <boost/foreach.hpp>

#ifdef _MSC_VER
# pragma warning (disable:4996)   // C4996: use POSIX function (wcscat)
#endif // _MSC_VER

//#define DEBUG "myfunc"
#ifdef DEBUG
# include "growl.h"
#endif // DEBUG

// - Helpers -

namespace { // unnamed
const MyFunctionInfo MY_FUNCTIONS[] = { MY_FUNCTIONS_INITIALIZER };

enum { PATH_SEP = '\\' };

inline const char *basename(const char *s)
{
  if (const char *ret = ::strrchr(s, PATH_SEP))
    return ++ret;
  else
    return s;
}

inline const wchar_t *basename(const wchar_t *s)
{
  if (const wchar_t *ret = ::wcsrchr(s, PATH_SEP))
    return ++ret; // skip the path seperator
  else
    return s;
}

inline LPCWSTR applicationPathW()
{
  static WCHAR ret[MAX_PATH];
  if (!*ret)
    ::GetModuleFileNameW(nullptr, ret, MAX_PATH);
  return ret;
}

inline LPCWSTR applicationNameW()
{
  static LPCWSTR ret = nullptr;
  if (!ret && (ret = wcsrchr(applicationPathW(), PATH_SEP)))
    ret++;  // skip the path seperator
  return ret;
}

} // unnamed namespace

// - Hooker -

//BOOL (WINAPI *OldTextOutA)(HDC hdc, int nXStart, int nYStart, LPCSTR lpString, int cchString) = TextOutA;
void My::OverrideModuleFunctions(HMODULE hModule)
{
  // http://social.msdn.microsoft.com/Forums/en-US/vcgeneral/thread/ef4a6bdd-6e9f-4f0a-9096-ca07ad65ddc2/
  // http://stackoverflow.com/questions/3263688/using-detours-for-hooking-writing-text-in-notepad
  //
  //::DetourRestoreAfterWith();
  //::DetourTransactionBegin();
  //::DetourUpdateThread(::GetCurrentThread());
  //::DetourAttach((PVOID *)&OldTextOutA, MyTextOutA);
  //::DetourTransactionCommit();

  BOOST_FOREACH (const MyFunctionInfo &fn, MY_FUNCTIONS) {
#ifdef DEBUG
    PVOID ret = winsec::OverrideFunctionA(hModule, fn.moduleName, fn.functionName, fn.functionAddress);
    if (ret)
      growl::show(fn.functionName); // success
#else
    winsec::OverrideFunctionA(hModule, fn.moduleName, fn.functionName, fn.functionAddress);
#endif // DEBUG
  }
}

void My::OverrideModules()
{
  LPCWSTR exeName = applicationNameW();
  if (!exeName)
    return;
  LPCWSTR exePath = applicationPathW();

  // For each matched module, override functions
  enum { MAX_MODULE = 0x800 };
  WCHAR path[MAX_PATH];
  HMODULE modules[MAX_MODULE];
  DWORD size;
  if (::EnumProcessModules(::GetCurrentProcess(), modules, sizeof(modules), &size) && (size/=4))
    for (size_t i = 0; i < size; i++)
      if (::GetModuleFileNameW(modules[i], path, sizeof(path)/sizeof(*path)) &&
          !::wcsnicmp(path, exePath, exeName - exePath))
        OverrideModuleFunctions(modules[i]);
}

// - My Functions -

HMODULE WINAPI MyLoadLibrary(_In_ LPCTSTR lpFileName)
{
  HMODULE ret = ::LoadLibrary(lpFileName);
  if (!::GetModuleHandle(lpFileName)) // this is the first load
    My::OverrideModuleFunctions(ret);
  return ret;
}

HMODULE WINAPI MyLoadLibraryEx(_In_ LPCTSTR lpFileName, __reserved HANDLE hFile, _In_ DWORD dwFlags)
{
  HMODULE ret = ::LoadLibraryEx(lpFileName, hFile, dwFlags);
  if (!::GetModuleHandle(lpFileName)) // this is the first load
    My::OverrideModuleFunctions(ret);
  return ret;
}

LPVOID WINAPI MyGetProcAddress(HMODULE hModule, LPCSTR lpProcName)
{
  char modulePath[MAX_PATH];
  if (::GetModuleFileNameA(hModule, modulePath, MAX_PATH)) {
    const char *moduleName = ::basename(modulePath);
    BOOST_FOREACH (const MyFunctionInfo &fn, MY_FUNCTIONS)
      if (!::stricmp(moduleName, fn.moduleName) && !::stricmp(lpProcName, fn.functionName))
        return fn.functionAddress;
  }
  return ::GetProcAddress(hModule, lpProcName);
}

BOOL WINAPI MyTrackPopupMenu(HMENU hMenu, UINT uFlags, int x, int y, int nReserved, HWND hWnd, CONST RECT * prcRect)
{
  //if (HANDLE hThread = CreateThread(0, 0, TranslateMenuThreadProc, hMenu, 0, 0))
  //  CloseHandle(hThread);
  if (MainObject::instance())
    MainObject::instance()->updateContextMenu(hMenu, hWnd);
  return ::TrackPopupMenu(hMenu, uFlags, x, y, nReserved, hWnd, prcRect);
}

BOOL WINAPI MyTrackPopupMenuEx(HMENU hMenu, UINT uFlags, int x, int y, HWND hWnd, LPTPMPARAMS lptpm)
{
  //if (HANDLE hThread = CreateThread(0, 0, TranslateMenuThreadProc, hMenu, 0, 0))
  //  CloseHandle(hThread);
  if (MainObject::instance())
    MainObject::instance()->updateContextMenu(hMenu, hWnd);
  return ::TrackPopupMenuEx(hMenu, uFlags, x, y, hWnd, lptpm);
}

// - GDI -

// CHECKPOINT
/*
#include "growl.h"
static int i = 0;
BOOL WINAPI MyTextOutA(HDC hdc, int nXStart, int nYStart, LPCSTR lpString, int cchString)
{
  //growl::warn(QString("%1, %2").arg(QString::number(nXStart)).arg(QString::number(nYStart)));
  int j = i++;
  if (j < 0)
    return true;
  Q_UNUSED(lpString);
  Q_UNUSED(cchString);
  QString s = "Konnichiwa, minasama >_<"; // dividable by 2
  //if (j % 3)
  //  return true;;
  //j /= 3;
  //if (j >= s.size()/2)
  //  return true;
  int index =  j % (s.size()/2);
  QString t = s.mid(index*2, 2);
  return ::TextOutA(hdc, nXStart, nYStart, t.toLocal8Bit(), t.size());
}
*/

//BOOL WINAPI MyTextOutW(HDC hdc, int nXStart, int nYStart, LPCWSTR lpString, int cchString)
//{
//  growl::warn("TextOutW");
//  return ::TextOutW(hdc, nXStart, nYStart, lpString, cchString);
//}

// - D3D -

//IDirect3D9* WINAPI MyDirect3DCreate9(UINT SDKVersion)
//{
//  growl::warn("d3d");
//  return ::Direct3DCreate9(SDKVersion);
//}

// EOF

/*
HRESULT MyD3DXCreateFontA(
  _In_ LPDIRECT3DDEVICE9 pDevice,
  _In_ INT Height,
  _In_ UINT Width,
  _In_ UINT Weight,
  _In_ UINT MipLevels,
  _In_ BOOL Italic,
  _In_ DWORD CharSet,
  _In_ DWORD OutputPrecision,
  _In_ DWORD Quality,
  _In_ DWORD PitchAndFamily,
  _In_ LPCSTR pFacename,
  _Out_ LPD3DXFONT *ppFont
)
{
  growl::warn(L"message A");
  return ::D3DXCreateFontA(pDevice, Height, Width, Weight, MipLevels, Italic, CharSet, OutputPrecision, Quality, PitchAndFamily, pFacename, ppFont);
}
HRESULT MyD3DXCreateFontW(
  _In_ LPDIRECT3DDEVICE9 pDevice,
  _In_ INT Height,
  _In_ UINT Width,
  _In_ UINT Weight,
  _In_ UINT MipLevels,
  _In_ BOOL Italic,
  _In_ DWORD CharSet,
  _In_ DWORD OutputPrecision,
  _In_ DWORD Quality,
  _In_ DWORD PitchAndFamily,
  _In_ LPCWSTR pFacename,
  _Out_ LPD3DXFONT *ppFont
)
{
  growl::warn(L"message W");
  return ::D3DXCreateFontW(pDevice, Height, Width, Weight, MipLevels, Italic, CharSet, OutputPrecision, Quality, PitchAndFamily, pFacename, ppFont);
}

  BOOL CALLBACK EnumThreadWndProc(HWND hWnd, LPARAM params)
  {
    CC_USED(params);

    if (!SupportsWindowClass(hWnd))
      return TRUE;

      if (!strcmp(type, "SysTabControl32")) {
        int changed = 0;
        LRESULT count = SendMessage(hWnd, TCM_GETITEMCOUNT, 0, 0);
        TCITEM item;
        for (int i=0; i<count; i++) {
          item.mask = TCIF_TEXT;
          item.pszText = buffer->text;
          item.cchTextMax = buffer->size;
          if (!SendMessage(hWnd, TCM_GETITEM, i, (LPARAM)&item)) break;
          if (HasJap(buffer->text)) {
            wchar_t *eng = TranslateFullLog(buffer->text);
            if (eng) {
              item.mask = TCIF_TEXT;
              item.pszText = eng;
              SendMessage(hWnd, TCM_SETITEM,  i, (LPARAM)&item);
              changed = 1;
              free(eng);
              buffer->updated = 1;
            }
          }
        }
        if (changed) {
          Sleep(100);
          InvalidateRect(hWnd, 0, 1);
        }
      }
      else if (!strcmp(type, "SysListView32")) {
        int changed = 0;
        for (int i = 0; true; ++i) {
          LVCOLUMN column;
          column.mask = LVCF_TEXT;
          column.pszText = buffer->text;
          column.cchTextMax = buffer->size;
          if (TRUE != ListView_GetColumn(hWnd, i, &column))
            break;
          if (HasJap(buffer->text)) {
            wchar_t *eng = TranslateFullLog(buffer->text);
            if (eng) {
              column.mask = TCIF_TEXT;
              column.pszText = eng;
              ListView_SetColumn(hWnd, i, (LPARAM) &column);
              changed = 1;
              buffer->updated = 1;
              free(eng);
            }
          }
        }
        if (changed) {
          Sleep(100);
          InvalidateRect(hWnd, 0, 1);
        }
      }

    //int top = 0;
    //if (buffer->updated < 0) {
    //  top = 1;
    //  buffer->updated = 0;
    //}

    int len = GetWindowTextLengthW(hWnd);
    if (len > 0) {
      if (buffer->size < len+1) {
        buffer->size = len+150;
        buffer->text = (wchar_t*) realloc(buffer->text, sizeof(wchar_t)*buffer->size);
      }
      if (GetWindowTextW(hWnd, buffer->text, buffer->size)) {
        if (HasJap(buffer->text)) {
          wchar_t *eng = TranslateFullLog(buffer->text);
          if (eng) {
            SetWindowTextW(hWnd, eng);
            buffer->updated = 1;
            if (!strcmp(type, "Static") || !strcmp(type, "Button")) {
              HDC hDC = GetDC(hWnd);
              SIZE s;
              if (hDC) {
                int changed = 0;
                if (GetTextExtentPointW(hDC, eng, wcslen(eng), &s)) {
                  RECT client, window;
                  if (GetClientRect(hWnd, &client) && GetWindowRect(hWnd, &window)) {
                    int wantx = s.cx - client.right;
                    if (wantx < 0) wantx = 0;
                    int wanty = s.cy - client.bottom;
                    if (wanty < 0) wanty = 0;
                    if (wantx || wanty) {
                      HWND hParent = GetParent(hWnd);
                      if (hParent) {
                        FitInfo info;
                        info.hWnd = hWnd;
                        info.r = window;
                        info.target = window;
                        info.target.right += wantx;
                        info.target.bottom += wanty;
                        RECT pRect;
                        GetWindowRect(hParent, &pRect);
                        if (info.target.right >= pRect.right) {
                          info.target.right = pRect.right-5;
                          info.target.bottom = pRect.bottom;
                        }
                        EnumChildWindows(hParent, (WNDENUMPROC)EnumFitWndProc, (LPARAM)&info);
                        SetWindowPos(hWnd, 0, 0, 0, info.target.right-info.target.left, info.target.bottom-info.target.top, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER);
                      }
                    }
                  }
                }
                ReleaseDC(hWnd, hDC);
              }
            }
            free(eng);
          }
        }
      }
    }
    HMENU hMenu = GetMenu(hWnd);
    if (hMenu) {
      //if (ProcessMenu(hMenu, buffer)) {
      //  DrawMenuBar(hWnd);
      //}
      MainObject::instance()->addMenu(hWnd);
      MainObject::instance()->update();
    }
    EnumChildWindows(hWnd, (WNDENUMPROC)EnumThreadWndProc, nullptr);
    //if (top) {
    //  if (buffer->updated > 0)
    //    InvalidateRect(hWnd, 0, 1);
    //  buffer->updated = -1;
    //}
    return TRUE;
  }

  // Used to resize buttons/windows. TODO
  struct FitInfo {
    HWND hWnd;
    RECT r;
    RECT target;
  };

  BOOL CALLBACK EnumFitWndProc(HWND hWnd, FitInfo *fit) {
    if (fit->hWnd == hWnd)
      return TRUE;
    RECT r;
    if (!GetWindowRect(hWnd, &r))
      return TRUE;
    // inside the other.
    if (fit->r.right  < r.right  && fit->r.left > r.left &&
      fit->r.bottom < r.bottom && fit->r.top  > r.top) {
        if (fit->target.right > r.right-4) {
          fit->target.right = r.right-4;
        }
        if (fit->target.bottom > r.bottom-4) {
          fit->target.bottom = r.bottom-4;
        }
        return TRUE;
    }
    if (r.left < fit->target.right && r.right > fit->target.left) {
      if (r.bottom > fit->target.top && r.top <= fit->target.bottom) {
        fit->target.bottom = r.top - 1;
        if (fit->target.bottom < fit->r.bottom) {
          fit->target.bottom = fit->r.bottom;
        }
      }
    }
    if (r.top < fit->target.bottom && r.bottom > fit->target.top) {
      if (r.right > fit->target.left && r.left <= fit->target.right) {
        fit->target.right = r.left - 1;
        if (fit->target.right < fit->r.right) {
          fit->target.right = fit->r.right;
        }
      }
    }
    return TRUE;
  }

int WINAPI MyMessageBoxA(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType) {
  //int len = -1;
  //wchar_t *uni1 = StringToUnicode(lpText, len);
  //len = -1;
  //wchar_t *uni2 = StringToUnicode(lpCaption, len);
  //int res = MyMessageBoxW(hWnd, uni1, uni2, uType);
  //free(uni1);
  //free(uni2);
  //return res;
  return MessageBoxA(hWnd, lpText, lpCaption, uType);
}

int WINAPI MyMessageBoxW(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType)
{
  //int ret = 0;
  ////if (AtlasIsLoaded()) {
  //if (true) {
  //  wchar_t *uni1 = TranslateFullLog(lpText);
  //  wchar_t *uni2 = TranslateFullLog(lpCaption);
  //  ret = MessageBoxW(hWnd, uni1, uni2, uType);
  //  free(uni1);
  //  free(uni2);
  //} else
  return MessageBoxW(hWnd, lpText, lpCaption, uType);
}
*/
