#include "window_decoration.hpp"

#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>

#include <dwmapi.h>
#endif

namespace mirrorfly
{
    void configure_transparent_window_frame(QWindow& window)
    {
#ifdef Q_OS_WIN
        const auto handle = reinterpret_cast<HWND>(window.winId());
        // QML owns the outline. Native non-client paint would surround transparent loading cards.
        const DWMNCRENDERINGPOLICY policy = DWMNCRP_DISABLED;
        DwmSetWindowAttribute(handle, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
        const COLORREF border = 0xFFFFFFFE;
        DwmSetWindowAttribute(handle, DWMWA_BORDER_COLOR, &border, sizeof(border));
#else
        Q_UNUSED(window);
#endif
    }
}
