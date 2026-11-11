#include <QApplication>
#include <QCursor>
#include <cassert>
#include <cstdio>
#include <cstring>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#include "qtui/QtRdpSessionWidget.h"

#include <cstdlib>

#undef assert
#define assert(condition) do { if (!(condition)) { std::fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); std::exit(EXIT_FAILURE); } } while (false)
struct RdpCursorLifecycleTestAccess
{
    static void run(const char *scenario)
    {
        QtRdpSessionWidget widget(Profile{});
        widget.m_process = std::make_shared<FreeRdpProcess>();
        widget.m_process->updateStateFromBackend(FreeRdpProcess::State::Running);
        widget.updateState(FreeRdpProcess::State::Running);
        widget.resize(300, 200);
        widget.show();
        QApplication::setActiveWindow(&widget);
        widget.setFocus();
        QApplication::processEvents();
        assert(widget.hasFocus());
        widget.m_process->updateCursorFromBackend({CursorKind::Hidden, nullptr, false});
        widget.updateCursor();
        assert(widget.cursor().shape() == Qt::BlankCursor);

        if (std::strcmp(scenario, "stop") == 0) {
            widget.stopProcess();
        } else {
            widget.m_process->updateStateFromBackend(FreeRdpProcess::State::Finished);
            widget.updateState(FreeRdpProcess::State::Finished);
            if (std::strcmp(scenario, "late") == 0)
                widget.updateCursor(); // a cursor callback queued before disconnect
        }
        assert(widget.cursor().shape() == Qt::ArrowCursor);
        assert(!widget.m_remoteCursor.hasCursor());
        // A new running session can still explicitly request a hidden cursor.
        widget.m_process->updateStateFromBackend(FreeRdpProcess::State::Running);
        widget.updateState(FreeRdpProcess::State::Running);
        widget.m_process->updateCursorFromBackend({CursorKind::Hidden, nullptr, false});
        widget.updateCursor();
        assert(widget.cursor().shape() == Qt::BlankCursor);
        widget.m_process->resetCursorFromBackend();
        widget.updateCursor();
        assert(widget.cursor().shape() == Qt::ArrowCursor);
        widget.close();
    }
};

int main(int argc, char **argv)
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    QApplication app(argc, argv);
    RdpCursorLifecycleTestAccess::run(argc > 1 ? argv[1] : "finished");
    return 0;
}
