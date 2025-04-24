#include <Windows.h>
#include <Shellapi.h>
#include <TlHelp32.h>
#include <iostream>
#include <string>

int g_waitCount = 20;

#ifndef MAKEULONGLONG
#define MAKEULONGLONG(ldw, hdw) ((ULONGLONG(hdw) << 32) | ((ldw)&0xFFFFFFFF))
#endif

#ifndef MAXULONGLONG
#define MAXULONGLONG ((ULONGLONG) ~((ULONGLONG)0))
#endif

DWORD getProcMainThreadId(DWORD dwProcID)
{
    DWORD dwMainThreadID = 0;
    ULONGLONG ullMinCreateTime = MAXULONGLONG;

    HANDLE hThreadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hThreadSnap != INVALID_HANDLE_VALUE)
    {
        THREADENTRY32 th32;
        th32.dwSize = sizeof(THREADENTRY32);
        BOOL bOK = TRUE;
        for (bOK = Thread32First(hThreadSnap, &th32); bOK;
             bOK = Thread32Next(hThreadSnap, &th32))
        {
            if (dwMainThreadID == 0)
            {
                dwMainThreadID = th32.th32ThreadID;
            }
            if (th32.th32OwnerProcessID == dwProcID)
            {
                HANDLE hThread = OpenThread(THREAD_QUERY_INFORMATION,
                                            TRUE, th32.th32ThreadID);

                if (hThread)
                {
                    FILETIME afTimes[4] = {0};
                    if (GetThreadTimes(hThread,
                                       &afTimes[0], &afTimes[1], &afTimes[2], &afTimes[3]))
                    {
                        ULONGLONG ullTest = MAKEULONGLONG(afTimes[0].dwLowDateTime,
                                                          afTimes[0].dwHighDateTime);
                        if (ullTest && ullTest < ullMinCreateTime)
                        {
                            ullMinCreateTime = ullTest;
                            dwMainThreadID = th32.th32ThreadID; // let it be main... :)
                        }
                    }
                    CloseHandle(hThread);
                }
            }
        }
#ifndef UNDER_CE
        CloseHandle(hThreadSnap);
#else
        CloseToolhelp32Snapshot(hThreadSnap);
#endif
    }
    return dwMainThreadID;
}

bool safeInjectDll(DWORD pid, DWORD threadId, const std::wstring &dll)
{
    typedef HHOOK(WINAPI * fn)(int, HOOKPROC, HINSTANCE, DWORD);
    HMODULE user32 = GetModuleHandleW(L"USER32");
    fn set_windows_hook_ex;
    HMODULE lib = LoadLibraryExW(dll.c_str(), 0, DONT_RESOLVE_DLL_REFERENCES);
    LPVOID proc;
    HHOOK hook;

    if (!lib || !user32)
    {
        if (!lib)
        {
#ifdef _DEBUG
            std::wcout << L"LoadLibraryW failed:" << dll;
#endif
        }
        if (!user32)
        {
#ifdef _DEBUG
            std::wcout << L"USER32 module not found:" << dll;
#endif
        }
        return false;
    }

#ifdef _WIN64
    proc = GetProcAddress(lib, "msg_hook_proc_ov");
#else
    proc = GetProcAddress(lib, "_msg_hook_proc_ov@12");
#endif

    if (!proc)
    {
#ifdef _DEBUG
        std::wcout << L"GetProcAddress msg_hook_proc_ov failed";
#endif
        return false;
    }

    set_windows_hook_ex = (fn)GetProcAddress(user32, "SetWindowsHookExA");
    if (threadId == 0)
    {
        threadId = getProcMainThreadId(pid);
    }

#ifdef _DEBUG
    std::wcout << "hook "
               << "pid: " << pid << ", thread:" << threadId;
#endif

    hook = set_windows_hook_ex(WH_GETMESSAGE, (HOOKPROC)proc, lib, threadId);
    if (!hook)
    {
        DWORD err = GetLastError();
#ifdef _DEBUG
        std::wcout << L"SetWindowsHookEx failed: " << err;
#endif
        return false;
    }

    for (auto i = 0; i < g_waitCount; i++)
    {
        Sleep(500);
       //std::wcout << L"PostThreadMessage to hook window";

        PostThreadMessage(threadId, WM_USER + 432, 0, (LPARAM)hook);
    }
    return true;
}

bool startInject(DWORD pid, DWORD threadId, const std::wstring &dll)
{
    bool succeed = false;

    if (pid > 0)
    {
        if (safeInjectDll(pid, threadId, dll))
        {
            succeed = true;
        }
#ifdef _DEBUG
        std::wcout << L"safeInject, pid:" << pid << L", result:" << succeed;
#endif
    }

    return succeed;
}

int wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
    LPWSTR cmdlineRaw = GetCommandLineW();
    int numArgs = 0;
    LPWSTR *cmdLine = CommandLineToArgvW(cmdlineRaw, &numArgs);

    if (numArgs == 4)
    {
        std::wstring pidStr = cmdLine[1];
        std::wstring injectThreadStr = cmdLine[2];
        std::wstring dll = cmdLine[3];

        DWORD pid = std::stoul(pidStr);
        DWORD threadId = std::stoul(injectThreadStr);

        bool succeed = startInject(pid, threadId, dll);

        return succeed ? 0 : 1;
    }
    else if (numArgs == 3)
    {

        std::wstring windowHandle = cmdLine[1];
        std::wstring dll = cmdLine[2];
        const HWND hwnd = (HWND)(LONG_PTR)std::stoul(windowHandle);

        if (hwnd)
        {
            DWORD pId = NULL;
            DWORD threadId = GetWindowThreadProcessId(hwnd, &pId);
            bool succeed = startInject(pId, threadId, dll);
            return succeed ? 0 : 1;
        }
    }

    return -1;
}
