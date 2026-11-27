#pragma once
#include <windows.h>
#include <string>
#include "RenameIds.h"

namespace DeviceNames {
constexpr int MaxLength = 40;
constexpr wchar_t Store[] = L"Software\\BlueBatteryFloat\\DeviceNames";

inline std::wstring Read(const std::wstring& key, const wchar_t* store) {
    if (key.empty()) return {};
    wchar_t value[MaxLength + 1]{};
    DWORD bytes = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, store, key.c_str(), RRF_RT_REG_SZ,
                     nullptr, value, &bytes) != ERROR_SUCCESS) return {};
    return value;
}

inline bool Save(const std::wstring& key, const std::wstring& alias, const wchar_t* store) {
    if (key.empty() || alias.size() > MaxLength) return false;
    if (alias.empty()) {
        LSTATUS status = RegDeleteKeyValueW(HKEY_CURRENT_USER, store, key.c_str());
        return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND;
    }
    return RegSetKeyValueW(HKEY_CURRENT_USER, store, key.c_str(), REG_SZ, alias.c_str(),
                           DWORD((alias.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

struct Edit {
    std::wstring key, original, alias;
    const wchar_t* store;
};

inline INT_PTR CALLBACK Dialog(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_INITDIALOG) {
        auto edit = reinterpret_cast<Edit*>(l);
        SetWindowLongPtrW(window, DWLP_USER, l);
        SetDlgItemTextW(window, IDC_ORIGINAL_NAME, edit->original.c_str());
        HWND input = GetDlgItem(window, IDC_DEVICE_ALIAS);
        SendMessageW(input, EM_SETLIMITTEXT, MaxLength, 0);
        SetWindowTextW(input, edit->alias.c_str());
        SetFocus(input);
        SendMessageW(input, EM_SETSEL, 0, -1);
        return FALSE;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(w) == IDCANCEL)) {
        EndDialog(window, IDCANCEL);
        return TRUE;
    }
    if (message != WM_COMMAND || (LOWORD(w) != IDOK && LOWORD(w) != IDC_RESET_NAME)) return FALSE;
    auto edit = reinterpret_cast<Edit*>(GetWindowLongPtrW(window, DWLP_USER));
    wchar_t buffer[MaxLength + 1]{};
    if (LOWORD(w) == IDOK) GetDlgItemTextW(window, IDC_DEVICE_ALIAS, buffer, MaxLength + 1);
    std::wstring value(buffer);
    const wchar_t whitespace[] = L" \t\r\n\u3000";
    size_t first = value.find_first_not_of(whitespace);
    value = first == std::wstring::npos ? L"" : value.substr(first, value.find_last_not_of(whitespace) - first + 1);
    if (!Save(edit->key, value, edit->store)) {
        MessageBoxW(window, L"无法保存设备名称，请重试。", L"设备改名", MB_OK | MB_ICONERROR);
        return TRUE;
    }
    edit->alias = std::move(value);
    EndDialog(window, IDOK);
    return TRUE;
}
}
