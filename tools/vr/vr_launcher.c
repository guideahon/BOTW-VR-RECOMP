// Savage Wind VR.exe (Windows, `make dist VR=1`): starts savagewind.exe from its own folder with the VR mode on
// (SAVAGEWIND_VR=1; docs/VR.md). Any SAVAGEWIND_VR* variable already set wins. Its arguments go to the game. The VR
// options themselves are on the game's Options screen (VR page).
#include <windows.h>
#include <wchar.h>

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR args, int show) {
    (void)inst, (void)prev, (void)show;
    if (!GetEnvironmentVariableW(L"SAVAGEWIND_VR", NULL, 0)) SetEnvironmentVariableW(L"SAVAGEWIND_VR", L"1");
    wchar_t dir[MAX_PATH], exe[MAX_PATH + 32], cmd[MAX_PATH + 32 + 4096];
    DWORD n = GetModuleFileNameW(NULL, dir, MAX_PATH);
    if (!n || n >= MAX_PATH) return 1;
    wchar_t *slash = wcsrchr(dir, L'\\');
    if (slash) *slash = 0;
    swprintf(exe, sizeof exe / sizeof *exe, L"%ls\\savagewind.exe", dir);
    swprintf(cmd, sizeof cmd / sizeof *cmd, L"\"%ls\" %ls", exe, args ? args : L"");
    STARTUPINFOW si = {sizeof si};
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE), si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    if (si.hStdOutput || si.hStdError) si.dwFlags |= STARTF_USESTDHANDLES;  // (started with them: a log, a test)
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(exe, cmd, NULL, NULL, TRUE, 0, NULL, dir, &si, &pi)) {  // (handles inherited: its output too)
        MessageBoxW(NULL, L"savagewind.exe was not found next to Savage Wind VR.exe.", L"Savage Wind VR", MB_ICONERROR);
        return 1;
    }
    // (waits for the game: whatever started this one - Steam, a shortcut, a test - sees the game's run and exit code)
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return (int)code;
}
