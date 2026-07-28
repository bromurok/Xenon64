#pragma once
typedef void (*BrowserActionFn)(void* param);
struct BrowserActionEntry {
    const char* name;
    BrowserActionFn action;
    void* param;
};
typedef BrowserActionEntry* lpBrowserActionEntry;
