#pragma once

#include <string>

namespace rdp::certificate_prompt
{
struct Challenge
{
    std::wstring host;
    int port = 0;
    std::wstring commonName;
    std::wstring subject;
    std::wstring issuer;
    std::wstring fingerprint;
    bool changed = false;
};

enum class PromptIcon
{
    Question,
    Warning,
};

struct Prompt
{
    std::wstring message;
    PromptIcon icon = PromptIcon::Question;
};

Prompt promptForChallenge(const Challenge &challenge);
}
