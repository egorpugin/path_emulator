/*
 * Copyright (C) 2016-2018, Egor Pugin
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string>
#include <Windows.h>

#ifndef PROG
#error PROG is undefined
#endif

#define MESSAGE(m) MessageBox(NULL, m, L"Exe trampoline: " PROG_NAME, MB_OK)

#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "user32.lib")

#if defined(CONSOLE) && CONSOLE == 0
int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
#else
int main()
{
#endif
    WIN32_FIND_DATA FindFileData;
    auto hFind = FindFirstFile(PROG, &FindFileData);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        MESSAGE(L"File '" PROG L"' not found!");
        return 1;
    }

    std::wstring cmd = GetCommandLine();
    int argc;
    auto argv = CommandLineToArgvW(cmd.c_str(), &argc);

    int o = cmd[0] == '\"' ? 1 : 0;
    cmd.replace(cmd.find(argv[0]) - o, wcslen(argv[0]) + o + o, L"\"" PROG L"\"");

    LocalFree(argv);

    STARTUPINFO si = { 0 };
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi = { 0 };
    if (!CreateProcess(PROG, &cmd[0], 0, 0, TRUE, 0, 0, 0, &si, &pi))
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
    {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return r;
    }

    MESSAGE(L"Cannot get exit code!");
    return 1;
}
