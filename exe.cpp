#include <string>
#include <Windows.h>

#ifndef PROG
#error PROG is undefined
#endif

#define MESSAGE(m) MessageBox(NULL, m, L"Exe trampoline: __fn__", MB_OK)

#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "user32.lib")

int wmain(int argc, wchar_t *argv[])
{
    WIN32_FIND_DATA FindFileData;
    auto hFind = FindFirstFile(PROG, &FindFileData);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        MESSAGE(L"File '" PROG L"' not found!");
        return 1;
    }

    std::wstring cmd = GetCommandLine();
    int o = cmd[0] == '\"' ? 1 : 0;
    cmd.replace(cmd.find(argv[0]) - o, wcslen(argv[0]) + o + o, L"\"" PROG L"\"");

    STARTUPINFO si = { 0 };
    PROCESS_INFORMATION pi = { 0 };
    if (!CreateProcess(PROG, &cmd[0], 0, 0, 0, 0, 0, 0, &si, &pi))
    {
        auto e = GetLastError();
        WCHAR lpszBuffer[8192] = { 0 };
        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, e, 0, lpszBuffer, 8192, NULL);
        std::wstring s = L"CreateProcess() failed: ";
        s += lpszBuffer;
        MESSAGE(s.c_str());
        return 1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD r;
    if (GetExitCodeProcess(pi.hProcess, &r))
        return r;

    MESSAGE(L"Cannot get exit code!");
    return 1;
}
