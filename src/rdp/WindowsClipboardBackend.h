#pragma once

#include "PlatformClipboardBackend.h"

#include <memory>

class WindowsClipboardBackend : public PlatformClipboardBackend
{
public:
    WindowsClipboardBackend();
    ~WindowsClipboardBackend() override;

    bool attach(CliprdrClientContext *context) override;
    void detach() override;

private:
    struct Private;

    std::unique_ptr<Private> m_d;
};
