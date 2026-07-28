#pragma once
#include <vector>
#include "typedefs.h"
#include "Font.h"
#include "Draw.h"
#include "GlobalVideo.h"

namespace ZLX {
class Browser {
public:
    Browser() : m_defaultColor(0xffffffff), m_selectedColor(0xffffffff), m_focusColor(0xffffffff), m_blurColor(0xffffffff), m_launchAction(nullptr), m_progress(0.0f), m_romCount(0) {}
    void Begin() {}
    void End() {}
    void Render() {}
    Font* getFont() { return &m_font; }

    void Alert(const char* msg) {}
    void AddAction(lpBrowserActionEntry action) { m_actions.push_back(action); }

    void SetDefaultColor(unsigned int c) { m_defaultColor = c; }
    void SetSelectedColor(unsigned int c) { m_selectedColor = c; }
    void SetFocusColor(unsigned int c) { m_focusColor = c; }
    void SetBlurColor(unsigned int c) { m_blurColor = c; }
    void SetLaunchAction(BrowserActionFn fn) { m_launchAction = fn; }
    void SetProgressValue(float v) { m_progress = v; }

    void Run(const char* dir) {
        // Enumerar ROMs reales del directorio (.z64/.n64/.v64)
        char searchPattern[512];
        sprintf(searchPattern, "%s\\*.*", dir);

        WIN32_FIND_DATA findData;
        HANDLE hFind = FindFirstFile(searchPattern, &findData);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                {
                    const char* name = findData.cFileName;
                    size_t len = strlen(name);
                    bool isRom = false;
                    if (len > 4)
                    {
                        const char* ext = name + len - 4;
                        if (_stricmp(ext, ".z64") == 0 || _stricmp(ext, ".n64") == 0 || _stricmp(ext, ".v64") == 0)
                            isRom = true;
                    }
                    if (isRom)
                    {
                        char* fullpath = (char*)malloc(512);
                        sprintf(fullpath, "%s\\%s", dir, name);

                        lpBrowserActionEntry action = new BrowserActionEntry();
                        char* displayName = (char*)malloc(strlen(name) + 1);
                        strcpy(displayName, name);
                        action->name = displayName;
                        action->action = m_launchAction;
                        action->param = fullpath;
                        m_actions.push_back(action);
                        m_romCount++;
                    }
                }
            } while (FindNextFile(hFind, &findData));
            FindClose(hFind);
        }

        // Loop de navegacion simple: D-pad arriba/abajo mueve seleccion, A confirma
        int selected = 0;
        m_font.Begin();
        for (;;)
        {
            XINPUT_STATE state;
            XInputGetState(0, &state);

            static bool prevUp = false, prevDown = false, prevA = false, prevStart = false;
            bool up = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0;
            bool down = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
            bool a = (state.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0;
            bool start = (state.Gamepad.wButtons & XINPUT_GAMEPAD_START) != 0;

            if (up && !prevUp && !m_actions.empty())
                selected = (selected - 1 + (int)m_actions.size()) % (int)m_actions.size();
            if (down && !prevDown && !m_actions.empty())
                selected = (selected + 1) % (int)m_actions.size();

if ((a && !prevA) || (start && !prevStart))
{
    if (!m_actions.empty() && m_actions[selected]->action)
    {
        m_actions[selected]->action(m_actions[selected]->param);
    }
}

            prevUp = up; prevDown = down; prevA = a; prevStart = start;

            // Dibujar lista: barra de color solido por seleccion (texto real pendiente)
            if (ZLX::g_pVideoDevice)
            {
                ZLX::g_pVideoDevice->Clear(CLEAR_COLOR_BUFFER, 0xff000000);
                for (size_t i = 0; i < m_actions.size(); ++i)
                {
                    unsigned int color = ((int)i == selected) ? m_selectedColor : 0xff404040;
                    ZLX::Draw::DrawColoredRect(40.0f, 40.0f + i * 30.0f, 560.0f, 24.0f, color);
                    if (m_actions[i]->name)
                        m_font.DrawTextF(m_actions[i]->name, -1, 48.0f, 46.0f + i * 30.0f);
                }
                ZLX::g_pVideoDevice->UpdateFrame(false);
            }
        }
    }

private:
    Font m_font;
    std::vector<lpBrowserActionEntry> m_actions;
    unsigned int m_defaultColor;
    unsigned int m_selectedColor;
    unsigned int m_focusColor;
    unsigned int m_blurColor;
    BrowserActionFn m_launchAction;
    float m_progress;
    int m_romCount;
};
}
