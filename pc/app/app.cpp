#include "app.h"
#include "tool_build_identity.h"

#include "main_frame.h"

BEGIN_MESSAGE_MAP(CRl78ProgrammerApp, CWinApp)
END_MESSAGE_MAP()

BOOL CRl78ProgrammerApp::InitInstance() {
    CWinApp::InitInstance();
    auto* frame = new CMainFrame();
    if (!frame->Create(nullptr, L"RL78 Programmer Tool v" TOOL_BUILD_VERSION_W,
                       WS_OVERLAPPEDWINDOW, CRect(0, 0, 1280, 840))) {
        delete frame;
        return FALSE;
    }
    m_pMainWnd = frame;
    frame->ShowWindow(SW_SHOWMAXIMIZED);
    frame->UpdateWindow();
    return TRUE;
}
