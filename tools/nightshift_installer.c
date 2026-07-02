#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define IDR_NIGHTSHIFT_EXE 101
#define IDR_SDL2_DLL 102
#define IDR_README 103
#define IDR_HOW_TO_PLAY 104
#define IDR_WINDOWS_TXT 105
#define IDR_LICENSE 106
#define IDR_PACKAGE_TXT 107

#define IDC_INSTALL 201
#define IDC_CANCEL 202
#define IDC_DESKTOP 203
#define IDC_INSTALL_DIR 204
#define IDC_BROWSE 205

#ifndef NIGHTSHIFT_VERSION
#define NIGHTSHIFT_VERSION "0.1.0"
#endif

typedef struct InstallFile {
    int id;
    const char *name;
} InstallFile;

static const InstallFile install_files[] = {
    {IDR_NIGHTSHIFT_EXE, "nightshift.exe"},
    {IDR_SDL2_DLL, "SDL2.dll"},
    {IDR_README, "README.md"},
    {IDR_HOW_TO_PLAY, "HOW_TO_PLAY.md"},
    {IDR_WINDOWS_TXT, "WINDOWS.txt"},
    {IDR_LICENSE, "LICENSE"},
    {IDR_PACKAGE_TXT, "PACKAGE.txt"},
};

static bool join_path(char *out, size_t out_size, const char *dir, const char *leaf);

typedef struct PromptState {
    char *install_dir;
    size_t install_dir_size;
    bool desktop_shortcut;
    bool done;
    int result;
    unsigned int dpi;
    HFONT font;
} PromptState;

static int scale_px(int value, unsigned int dpi)
{
    return MulDiv(value, (int)dpi, 96);
}

static unsigned int get_system_dpi(void)
{
    HDC dc = GetDC(NULL);
    if (dc == NULL) {
        return 96;
    }
    int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(NULL, dc);
    return dpi > 0 ? (unsigned int)dpi : 96;
}

static unsigned int get_window_dpi(HWND window)
{
    HMODULE user32 = GetModuleHandleA("user32.dll");
    if (user32 != NULL) {
        typedef UINT(WINAPI *GetDpiForWindowFn)(HWND);
        union {
            FARPROC proc;
            GetDpiForWindowFn fn;
        } get_dpi_for_window;
        get_dpi_for_window.proc = GetProcAddress(user32, "GetDpiForWindow");
        if (get_dpi_for_window.fn != NULL) {
            UINT dpi = get_dpi_for_window.fn(window);
            if (dpi > 0) {
                return (unsigned int)dpi;
            }
        }
    }
    return get_system_dpi();
}

static void init_dpi_awareness(void)
{
    HMODULE user32 = GetModuleHandleA("user32.dll");
    if (user32 == NULL) {
        return;
    }

    typedef BOOL(WINAPI *SetProcessDpiAwarenessContextFn)(HANDLE);
    union {
        FARPROC proc;
        SetProcessDpiAwarenessContextFn fn;
    } set_context;
    set_context.proc = GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    if (set_context.fn != NULL) {
        (void)set_context.fn((HANDLE)-4);
        return;
    }

    typedef BOOL(WINAPI *SetProcessDPIAwareFn)(void);
    union {
        FARPROC proc;
        SetProcessDPIAwareFn fn;
    } set_aware;
    set_aware.proc = GetProcAddress(user32, "SetProcessDPIAware");
    if (set_aware.fn != NULL) {
        (void)set_aware.fn();
    }
}

static HFONT create_ui_font(unsigned int dpi)
{
    int height = -MulDiv(9, (int)dpi, 72);
    return CreateFontA(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
}

static void set_child_font(HWND child, HFONT font)
{
    SendMessageA(child, WM_SETFONT, (WPARAM)font, TRUE);
}

static bool browse_for_install_dir(HWND owner, char *path, size_t path_size)
{
    BROWSEINFOA info;
    memset(&info, 0, sizeof(info));
    info.hwndOwner = owner;
    info.lpszTitle = "Choose where to install Night Shift";
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST item = SHBrowseForFolderA(&info);
    if (item == NULL) {
        return false;
    }

    char selected[MAX_PATH];
    bool ok = SHGetPathFromIDListA(item, selected) != FALSE;
    CoTaskMemFree(item);
    if (!ok) {
        return false;
    }

    if (!join_path(path, path_size, selected, "Night Shift")) {
        return false;
    }
    return true;
}

static LRESULT CALLBACK prompt_wnd_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    PromptState *state = (PromptState *)GetWindowLongPtrA(window, GWLP_USERDATA);

    if (message == WM_NCCREATE) {
        CREATESTRUCTA *create = (CREATESTRUCTA *)lparam;
        state = (PromptState *)create->lpCreateParams;
        SetWindowLongPtrA(window, GWLP_USERDATA, (LONG_PTR)state);
    }

    switch (message) {
    case WM_CREATE: {
        state->dpi = get_window_dpi(window);
        state->font = create_ui_font(state->dpi);

        int margin = scale_px(18, state->dpi);
        int title_y = scale_px(18, state->dpi);
        int body_y = scale_px(48, state->dpi);
        int path_y = scale_px(82, state->dpi);
        int check_y = scale_px(126, state->dpi);
        int button_y = scale_px(164, state->dpi);
        int width = scale_px(500, state->dpi);
        int button_w = scale_px(92, state->dpi);
        int button_h = scale_px(30, state->dpi);
        int browse_w = scale_px(82, state->dpi);
        int gap = scale_px(10, state->dpi);

        HWND title = CreateWindowExA(0, "STATIC", "Night Shift Setup", WS_CHILD | WS_VISIBLE,
                                     margin, title_y, width - margin * 2, scale_px(24, state->dpi),
                                     window, NULL, NULL, NULL);
        HWND body = CreateWindowExA(0, "STATIC", "Install Night Shift " NIGHTSHIFT_VERSION " to:",
                                    WS_CHILD | WS_VISIBLE, margin, body_y, width - margin * 2,
                                    scale_px(22, state->dpi), window, NULL, NULL, NULL);
        HWND path = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", state->install_dir,
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, margin, path_y,
                                    width - margin * 2 - browse_w - gap, scale_px(28, state->dpi),
                                    window, (HMENU)(INT_PTR)IDC_INSTALL_DIR, NULL, NULL);
        SendMessageA(path, EM_LIMITTEXT, (WPARAM)(state->install_dir_size - 1), 0);
        HWND browse = CreateWindowExA(0, "BUTTON", "Browse...",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                      width - margin - browse_w, path_y, browse_w, scale_px(28, state->dpi),
                                      window, (HMENU)(INT_PTR)IDC_BROWSE, NULL, NULL);
        HWND check = CreateWindowExA(0, "BUTTON", "Create desktop shortcut",
                                     WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                     margin, check_y, scale_px(240, state->dpi), scale_px(24, state->dpi),
                                     window, (HMENU)(INT_PTR)IDC_DESKTOP, NULL, NULL);
        HWND install = CreateWindowExA(0, "BUTTON", "Install",
                                       WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                       width - margin - button_w * 2 - scale_px(10, state->dpi),
                                       button_y, button_w, button_h,
                                       window, (HMENU)(INT_PTR)IDC_INSTALL, NULL, NULL);
        HWND cancel = CreateWindowExA(0, "BUTTON", "Cancel",
                                      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                      width - margin - button_w, button_y, button_w, button_h,
                                      window, (HMENU)(INT_PTR)IDC_CANCEL, NULL, NULL);

        HWND controls[] = {title, body, path, browse, check, install, cancel};
        for (size_t i = 0; i < sizeof(controls) / sizeof(controls[0]); i++) {
            set_child_font(controls[i], state->font);
        }
        SendMessageA(check, BM_SETCHECK, state->desktop_shortcut ? BST_CHECKED : BST_UNCHECKED, 0);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IDC_INSTALL: {
            HWND path = GetDlgItem(window, IDC_INSTALL_DIR);
            GetWindowTextA(path, state->install_dir, (int)state->install_dir_size);
            if (state->install_dir[0] == '\0') {
                MessageBoxA(window, "Choose an install location before continuing.", "Night Shift Setup", MB_ICONERROR);
                return 0;
            }
            HWND check = GetDlgItem(window, IDC_DESKTOP);
            state->desktop_shortcut = SendMessageA(check, BM_GETCHECK, 0, 0) == BST_CHECKED;
            state->result = IDOK;
            state->done = true;
            DestroyWindow(window);
            return 0;
        }
        case IDC_BROWSE: {
            char selected[MAX_PATH];
            if (browse_for_install_dir(window, selected, sizeof(selected))) {
                HWND path = GetDlgItem(window, IDC_INSTALL_DIR);
                SetWindowTextA(path, selected);
            }
            return 0;
        }
        case IDC_CANCEL:
            state->result = IDCANCEL;
            state->done = true;
            DestroyWindow(window);
            return 0;
        default:
            break;
        }
        break;
    case WM_CLOSE:
        state->result = IDCANCEL;
        state->done = true;
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        if (state != NULL && state->font != NULL) {
            DeleteObject(state->font);
            state->font = NULL;
        }
        return 0;
    default:
        break;
    }

    return DefWindowProcA(window, message, wparam, lparam);
}

static int prompt_install(HINSTANCE instance, char *install_dir, size_t install_dir_size, bool *desktop_shortcut)
{
    const char *class_name = "NightShiftSetupWindow";
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = prompt_wnd_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(instance, MAKEINTRESOURCE(1));
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = class_name;
    RegisterClassA(&wc);

    PromptState state;
    memset(&state, 0, sizeof(state));
    state.install_dir = install_dir;
    state.install_dir_size = install_dir_size;
    state.desktop_shortcut = *desktop_shortcut;
    state.result = IDCANCEL;
    state.dpi = get_system_dpi();

    int width = scale_px(500, state.dpi);
    int height = scale_px(250, state.dpi);
    int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

    HWND window = CreateWindowExA(WS_EX_DLGMODALFRAME, class_name, "Night Shift Setup",
                                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                  x, y, width, height, NULL, NULL, instance, &state);
    if (window == NULL) {
        char prompt[MAX_PATH + 128];
        snprintf(prompt, sizeof(prompt), "Install Night Shift %s to:\n\n%s", NIGHTSHIFT_VERSION, install_dir);
        return MessageBoxA(NULL, prompt, "Night Shift Setup", MB_OKCANCEL | MB_ICONINFORMATION);
    }

    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    MSG msg;
    while (!state.done && GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    *desktop_shortcut = state.desktop_shortcut;
    return state.result;
}

static bool has_arg(const char *cmdline, const char *arg)
{
    return cmdline != NULL && strstr(cmdline, arg) != NULL;
}

static bool get_arg_value(const char *cmdline, const char *prefix, char *out, size_t out_size)
{
    const char *pos = strstr(cmdline, prefix);
    if (pos == NULL || out_size == 0) {
        return false;
    }

    pos += strlen(prefix);
    if (*pos == '"') {
        pos++;
        size_t len = 0;
        while (pos[len] != '\0' && pos[len] != '"' && len + 1 < out_size) {
            len++;
        }
        memcpy(out, pos, len);
        out[len] = '\0';
        return len > 0;
    }

    size_t len = 0;
    bool consume_rest = strcmp(prefix, "/D=") == 0;
    while (pos[len] != '\0' && pos[len] != '"' && (consume_rest || pos[len] != ' ') && len + 1 < out_size) {
        len++;
    }
    while (len > 0 && (pos[len - 1] == ' ' || pos[len - 1] == '\t')) {
        len--;
    }
    memcpy(out, pos, len);
    out[len] = '\0';
    return len > 0;
}

static bool join_path(char *out, size_t out_size, const char *dir, const char *leaf)
{
    int written = snprintf(out, out_size, "%s\\%s", dir, leaf);
    return written > 0 && (size_t)written < out_size;
}

static bool make_directory_tree(const char *path)
{
    char partial[MAX_PATH];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(partial)) {
        return false;
    }

    memcpy(partial, path, len + 1);
    for (char *p = partial; *p != '\0'; p++) {
        if (*p == '\\' || *p == '/') {
            char saved = *p;
            *p = '\0';
            if (partial[0] != '\0' && partial[1] != ':') {
                if (!CreateDirectoryA(partial, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
                    return false;
                }
            }
            *p = saved;
        }
    }

    return CreateDirectoryA(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static bool write_resource(HINSTANCE instance, int id, const char *path)
{
    HRSRC resource = FindResourceA(instance, MAKEINTRESOURCEA(id), RT_RCDATA);
    if (resource == NULL) {
        return false;
    }

    DWORD size = SizeofResource(instance, resource);
    HGLOBAL loaded = LoadResource(instance, resource);
    const void *data = loaded != NULL ? LockResource(loaded) : NULL;
    if (data == NULL) {
        return false;
    }

    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD written = 0;
    BOOL ok = WriteFile(file, data, size, &written, NULL);
    CloseHandle(file);
    return ok && written == size;
}

static bool create_shortcut(const char *target, const char *working_dir)
{
    char desktop[MAX_PATH];
    char shortcut[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, desktop) != S_OK) {
        return false;
    }
    if (!join_path(shortcut, sizeof(shortcut), desktop, "Night Shift.lnk")) {
        return false;
    }

    HRESULT init = CoInitialize(NULL);
    bool initialized = SUCCEEDED(init);
    IShellLinkA *link = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkA, (void **)&link);
    if (FAILED(hr) || link == NULL) {
        if (initialized) {
            CoUninitialize();
        }
        return false;
    }

    link->lpVtbl->SetPath(link, target);
    link->lpVtbl->SetWorkingDirectory(link, working_dir);
    link->lpVtbl->SetDescription(link, "Night Shift");
    link->lpVtbl->SetIconLocation(link, target, 0);

    IPersistFile *persist = NULL;
    hr = link->lpVtbl->QueryInterface(link, &IID_IPersistFile, (void **)&persist);
    if (SUCCEEDED(hr) && persist != NULL) {
        WCHAR wide_path[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, shortcut, -1, wide_path, MAX_PATH);
        hr = persist->lpVtbl->Save(persist, wide_path, TRUE);
        persist->lpVtbl->Release(persist);
    }

    link->lpVtbl->Release(link);
    if (initialized) {
        CoUninitialize();
    }
    return SUCCEEDED(hr);
}

static bool default_install_dir(char *out, size_t out_size)
{
    char local[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, local) != S_OK) {
        return false;
    }
    return join_path(out, out_size, local, "Night Shift");
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR cmdline, int show)
{
    (void)previous;
    (void)show;
    init_dpi_awareness();

    char install_dir[MAX_PATH];
    if (!get_arg_value(cmdline, "/D=", install_dir, sizeof(install_dir))) {
        if (!default_install_dir(install_dir, sizeof(install_dir))) {
            MessageBoxA(NULL, "Could not find the local application data directory.", "Night Shift Setup", MB_ICONERROR);
            return 1;
        }
    }

    bool silent = has_arg(cmdline, "/S");
    bool desktop_shortcut = !has_arg(cmdline, "/NODESKTOP");
    if (!silent) {
        if (prompt_install(instance, install_dir, sizeof(install_dir), &desktop_shortcut) != IDOK) {
            return 2;
        }
    }

    if (!make_directory_tree(install_dir)) {
        MessageBoxA(NULL, "Could not create the install directory.", "Night Shift Setup", MB_ICONERROR);
        return 1;
    }

    for (size_t i = 0; i < sizeof(install_files) / sizeof(install_files[0]); i++) {
        char path[MAX_PATH];
        if (!join_path(path, sizeof(path), install_dir, install_files[i].name) ||
            !write_resource(instance, install_files[i].id, path)) {
            MessageBoxA(NULL, "Could not write one of the bundled files.", "Night Shift Setup", MB_ICONERROR);
            return 1;
        }
    }

    char exe_path[MAX_PATH];
    bool have_exe_path = join_path(exe_path, sizeof(exe_path), install_dir, "nightshift.exe");
    if (desktop_shortcut && have_exe_path) {
        (void)create_shortcut(exe_path, install_dir);
    }

    if (!silent) {
        if (MessageBoxA(NULL, "Night Shift has been installed. Launch it now?", "Night Shift Setup", MB_YESNO | MB_ICONINFORMATION) == IDYES &&
            have_exe_path) {
            ShellExecuteA(NULL, "open", exe_path, NULL, install_dir, SW_SHOWNORMAL);
        }
    }

    return 0;
}
