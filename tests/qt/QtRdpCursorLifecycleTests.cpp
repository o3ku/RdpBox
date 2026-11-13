#include <QApplication>
#include <QCursor>
#include <cassert>
#include <algorithm>
#include <cstdio>
#include <cstring>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#include "qtui/QtRdpSessionWidget.h"
#include <freerdp/freerdp.h>
#include <freerdp/gdi/gdi.h>
#include <QColor>
#include <QImage>

#include <cstdlib>

#undef assert
#define assert(condition) do { if (!(condition)) { std::fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); std::exit(EXIT_FAILURE); } } while (false)
struct RdpCursorLifecycleTestAccess
{
    static void runFrames()
    {
        QtRdpSessionWidget widget(Profile{});
        widget.resize(80, 60);
        widget.m_process->updateStateFromBackend(FreeRdpProcess::State::Running);
        widget.updateState(FreeRdpProcess::State::Running);
        widget.m_frameGate = {};
        std::vector<BYTE> pixels(80 * 60 * 4, 0xFF);
        rdpGdi gdi{};
        gdi.width = 80;
        gdi.height = 60;
        gdi.stride = 80 * 4;
        gdi.primary_buffer = pixels.data();
        rdpContext context{};
        context.gdi = &gdi;
        widget.m_process->writeFrameFromContext(&context);
        QApplication::processEvents();
        assert(widget.m_frame.width == 80 && widget.m_frame.height == 60);
        for (int i = 0; i < 1000; ++i) {
            std::fill(pixels.begin(), pixels.end(), i == 999 ? 0xFF : 0);
            widget.m_process->writeFrameFromContext(&context);
        }
        assert(!widget.m_frameWakeup->request());
        QApplication::processEvents();
        assert(widget.m_frame.pixels.front() == 0xFF);
        const auto shown = widget.m_frame.pixels;
        const auto* storage = widget.m_frame.pixels.data();
        std::fill(pixels.begin(), pixels.end(), 0);
        widget.m_process->writeFrameFromContext(&context);
        // Finished retires the undelivered frame and invalidates its queued wake.
        widget.m_process->updateStateFromBackend(FreeRdpProcess::State::Finished);
        widget.updateState(FreeRdpProcess::State::Finished);
        widget.m_process->writeFrameFromContext(&context);
        widget.consumeFrame();
        QApplication::processEvents();
        assert(widget.m_frame.pixels == shown && widget.m_frame.pixels.data() == storage);
        assert(!widget.m_overlayText.isEmpty());
        assert(!widget.m_frameWakeup->request());
        QImage rendered(widget.size(), QImage::Format_ARGB32);
        widget.render(&rendered);
        assert(rendered.pixelColor(0, 0) == QColor(Qt::white));
        widget.stopProcess(true);
        assert(widget.m_frame.pixels == shown);
        // A fresh binding does not revive an old queued wake or frozen process.
        const auto oldWake = widget.m_frameWakeup;
        widget.m_process = std::make_shared<FreeRdpProcess>();
        widget.bindProcessCallbacks();
        widget.m_frameGeneration = 0;
        widget.m_process->updateStateFromBackend(FreeRdpProcess::State::Running);
        widget.updateState(FreeRdpProcess::State::Running);
        widget.m_process->writeFrameFromContext(&context);
        widget.consumeFrame();
        assert(widget.m_frame.pixels != shown);
        // Queued callbacks from a prior binding of the SAME process are ignored.
        widget.m_process->updateStateFromBackend(FreeRdpProcess::State::Finished);
        const auto staleWake = widget.m_frameWakeup;
        widget.bindProcessCallbacks();
        widget.updateState(FreeRdpProcess::State::Starting);
        QApplication::processEvents();
        assert(widget.m_state == FreeRdpProcess::State::Starting);
        assert(!staleWake->isOpen() && widget.m_frameWakeup->isOpen());
        assert(!oldWake->beginProcessing());
    }
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
    if (argc > 1 && std::strcmp(argv[1], "frames") == 0)
        RdpCursorLifecycleTestAccess::runFrames();
    else
        RdpCursorLifecycleTestAccess::run(argc > 1 ? argv[1] : "finished");
    return 0;
}
