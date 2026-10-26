#include "ILexer.h"
#include "LexerModule.h"
#include <cstddef>
#include <cstring>
extern const Lexilla::LexerModule lmCPP, lmPython, lmJSON, lmHTML, lmCss, lmMarkdown, lmBash,
    lmPowerShell, lmProps, lmYAML;
Scintilla::ILexer5* MakeLexer(const char* name) {
    using namespace Lexilla;
    const LexerModule* modules[]{&lmCPP,      &lmPython, &lmJSON,       &lmHTML,  &lmCss,
                                 &lmMarkdown, &lmBash,   &lmPowerShell, &lmProps, &lmYAML};
    for (auto m : modules)
        if (std::strcmp(m->languageName, name) == 0)
            return m->Create();
    return nullptr;
}
