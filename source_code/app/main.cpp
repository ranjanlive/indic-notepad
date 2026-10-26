#include "ILexer.h"
#include "SciLexer.h"
#include "Scintilla.h"
#include "core/core.h"
#include <algorithm>
#include <chrono>
#include <commctrl.h>
#include <commdlg.h>
#include <deque>
#include <dwmapi.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <richedit.h>
#include <shellapi.h>
#include <shlobj.h>
#include <sstream>
#include <thread>
#include <uxtheme.h>
#include <windows.h>
#include <windowsx.h>
Scintilla::ILexer5* MakeLexer(const char*);
using namespace np;
namespace {
constexpr UINT JOB_DONE = WM_APP + 1;
constexpr UINT TEST_RUN = WM_APP + 2;
constexpr UINT HISTORY_LIMIT = WM_APP + 3;
constexpr UINT INPUT_LIMIT = WM_APP + 4;
enum Command {
    New = 100,
    Open,
    Save,
    SaveAs,
    SaveAll,
    Close,
    CloseOthers,
    Exit,
    Reload,
    OpenFolder,
    Undo,
    Redo,
    Cut,
    Copy,
    Paste,
    SelectAll,
    Find,
    Replace,
    FindNext,
    FindPrev,
    GoTo,
    Wrap,
    Lines,
    Whitespace,
    ZoomIn,
    ZoomOut,
    ZoomReset,
    Dark,
    Light,
    System,
    Sidebar,
    Preview,
    Focus,
    Font,
    Palette,
    Recent,
    ClearRecent,
    RestoreSession,
    Recovery,
    ClearRecovery,
    Help,
    About,
    NextTab,
    PrevTab,
    MoveLeft,
    MoveRight,
    History,
    Enc8,
    Enc8Bom,
    Enc16LE,
    Enc16BE,
    EolLF,
    EolCRLF,
    EolCR,
    FindEdit = 300,
    ReplaceEdit,
    CaseCheck,
    WordCheck,
    RegexCheck,
    NextButton,
    PrevButton,
    ReplaceButton,
    ReplaceAllButton,
    CloseSearch,
    Tree = 400,
    Tabs,
    ReaderPane,
    GoEdit,
    GoOkay,
    GoCancel,
    PaletteEdit,
    PaletteList
};
struct Colors {
    COLORREF bg, panel, editor, text, muted, line, accent, selected, green;
};
struct Doc {
    uint64_t id{}, revision{}, chars{}, bytes{}, undo_bytes{};
    sptr_t pointer{};
    fs::path path;
    std::wstring title;
    Encoding encoding{Encoding::utf8};
    Eol eol{Eol::lf};
    Stamp stamp;
    bool metadata_dirty{}, text_dirty{}, read_only{}, truncated{};
    sptr_t cursor{}, anchor{}, first_line{};
};
struct App {
    HINSTANCE instance{};
    HWND window{}, editor{}, tabs{}, tree{}, reader{}, search_panel{}, find_edit{}, replace_edit{},
        case_check{}, word_check{}, regex_check{};
    std::vector<HWND> toolbar, search_buttons;
    std::vector<std::unique_ptr<Doc>> docs;
    int active{-1}, dpi{96}, theme{2}, zoom{}, font_size{13};
    uint64_t next_id{};
    UINT benchmark_ms{};
    uint64_t paint_count{}, update_count{}, app_timer_count{};
    HMODULE rich_module{};
    bool dark{}, high_contrast{}, wrap{true}, numbers{true}, whitespace{}, sidebar{}, preview{},
        focus{}, recovery{true}, restore{true}, history{true}, loading{}, busy{},
        deferred_recovery{}, quitting{}, search_pending{};
    bool read_only_arg{}, preserve_recovery{};
    std::atomic_bool cancel{};
    std::jthread worker;
    Colors c{};
    HBRUSH bg_brush{}, panel_brush{}, editor_brush{};
    HFONT ui_font{}, small_font{}, bold_font{}, title_font{};
    HICON icon{};
    fs::path data_dir, workspace, test_dir;
    std::unique_ptr<Handle> profile_lock;
    std::vector<fs::path> recent;
    std::deque<fs::path> pending_files;
    std::deque<std::wstring> menu_labels;
    std::vector<std::pair<int, std::wstring>> palette_commands;
    std::vector<std::pair<HTREEITEM, fs::path>> tree_paths;
    std::wstring status{L"Ready"}, font_name{L"Consolas"};
    std::string last_pattern, last_replacement;
    SearchOptions last_options{};
    int editor_top{}, editor_bottom{}, sidebar_width{230}, preview_width{330}, drag_tab{-1},
        resize_pane{};
    POINT drag_start{};
    ~App() {
        if (rich_module)
            FreeLibrary(rich_module);
        if (ui_font)
            DeleteObject(ui_font);
        if (small_font)
            DeleteObject(small_font);
        if (bold_font)
            DeleteObject(bold_font);
        if (title_font)
            DeleteObject(title_font);
        if (bg_brush)
            DeleteObject(bg_brush);
        if (panel_brush)
            DeleteObject(panel_brush);
        if (editor_brush)
            DeleteObject(editor_brush);
    }
    int s(int n) const {
        return MulDiv(n, dpi, 96);
    }
    sptr_t sci(UINT msg, uptr_t w = 0, sptr_t l = 0) const {
        return SendMessageW(editor, msg, w, l);
    }
    Doc* current() {
        return active >= 0 && active < int(docs.size()) ? docs[size_t(active)].get() : nullptr;
    }
    Doc* by_id(uint64_t id) {
        for (auto& d : docs)
            if (d->id == id)
                return d.get();
        return nullptr;
    }
    std::string text() {
        size_t n = size_t(sci(SCI_GETLENGTH));
        std::string out(n + 1, '\0');
        sci(SCI_GETTEXT, n + 1, reinterpret_cast<sptr_t>(out.data()));
        out.resize(n);
        return out;
    }
    std::string doc_text(Doc& d) {
        if (&d == current())
            return text();
        auto old = sci(SCI_GETDOCPOINTER);
        auto cursor = sci(SCI_GETCURRENTPOS), anchor = sci(SCI_GETANCHOR),
             scroll = sci(SCI_GETFIRSTVISIBLELINE);
        loading = true;
        sci(SCI_SETDOCPOINTER, 0, d.pointer);
        auto out = text();
        sci(SCI_SETDOCPOINTER, 0, old);
        sci(SCI_SETSEL, anchor, cursor);
        sci(SCI_SETFIRSTVISIBLELINE, scroll);
        loading = false;
        return out;
    }
    bool dirty(Doc& d) {
        return d.metadata_dirty || d.text_dirty;
    }
    void error(const std::string& message) {
        status = L"Action needs attention";
        MessageBoxW(window, wide(message).c_str(), L"Indic NotePad", MB_OK | MB_ICONWARNING);
        invalidate();
    }
    void invalidate() {
        if (window)
            InvalidateRect(window, nullptr, FALSE);
    }
    void command(int id);
    void layout();
    void paint();
    void apply_theme();
    void style_editor();
    void update_tabs();
    void select(int n);
    void add_document(Loaded d, std::wstring title = L"");
    void new_document();
    void open_file(fs::path p, bool preview_ok = false);
    void open_dialog();
    bool save_doc(bool as = false);
    bool close_doc(int n);
    bool close_window();
    void save_all();
    void refresh_preview();
    void schedule_recovery();
    void checkpoint();
    void load_settings();
    void save_settings();
    void restore_session();
    void show_search(bool replace);
    void find(bool previous = false, int operation = 0);
    void begin_job(std::function<std::function<void()>()> job);
    void changed();
    HMENU menubar{};
    HMENU make_menu();
    void add_menu(HMENU menu, UINT_PTR id, const std::wstring& label, UINT flags = 0);
    void show_palette();
    void show_goto();
    void browse_folder();
    void populate_tree(HTREEITEM parent, const fs::path& path);
    void change_encoding(Encoding enc);
    void change_eol(Eol eol);
    void run_tests();
} app;
void fill(HDC dc, RECT r, COLORREF color) {
    auto b = CreateSolidBrush(color);
    FillRect(dc, &r, b);
    DeleteObject(b);
}
void line(HDC dc, int x, int y, int x2, int y2, COLORREF color) {
    auto p = CreatePen(PS_SOLID, 1, color);
    auto old = SelectObject(dc, p);
    MoveToEx(dc, x, y, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, old);
    DeleteObject(p);
}
void label(HDC dc, std::wstring_view text, RECT r, HFONT font, COLORREF color,
           UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.data(), int(text.size()), &r, flags);
}
std::wstring display_path(const fs::path& p) {
    auto s = p.wstring();
    if (s.starts_with(L"\\\\?\\UNC\\"))
        return L"\\\\" + s.substr(8);
    if (s.starts_with(L"\\\\?\\"))
        return s.substr(4);
    return s;
}
std::wstring get_text(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(size_t(n) + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(size_t(n));
    return s;
}
HWND button(HWND parent, int id, const wchar_t* text) {
    auto h =
        CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0,
                      0, parent, reinterpret_cast<HMENU>(INT_PTR(id)), app.instance, nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(app.ui_font), TRUE);
    return h;
}
int eol_mode(Eol e) {
    return e == Eol::crlf ? SC_EOL_CRLF : e == Eol::cr ? SC_EOL_CR : SC_EOL_LF;
}
void App::add_menu(HMENU m, UINT_PTR id, const std::wstring& name, UINT flags) {
    menu_labels.push_back(name);
    AppendMenuW(m, flags | MF_OWNERDRAW, id, reinterpret_cast<const wchar_t*>(&menu_labels.back()));
    MENUITEMINFOW info{sizeof(info)};
    info.fMask = MIIM_STRING | MIIM_DATA;
    info.dwTypeData = menu_labels.back().data();
    info.dwItemData = reinterpret_cast<ULONG_PTR>(&menu_labels.back());
    SetMenuItemInfoW(m, UINT(GetMenuItemCount(m) - 1), TRUE, &info);
}
HMENU App::make_menu() {
    menu_labels.clear();
    palette_commands.clear();
    auto root = CreateMenu();
    auto file = CreatePopupMenu(), edit = CreatePopupMenu(), view = CreatePopupMenu(),
         search = CreatePopupMenu(), tools = CreatePopupMenu(), windows = CreatePopupMenu(),
         help = CreatePopupMenu();
    auto add = [&](HMENU m, int id, const wchar_t* name) {
        add_menu(m, UINT(id), name);
        std::wstring plain = name;
        std::erase(plain, L'&');
        palette_commands.push_back({id, plain});
    };
    add(file, New, L"&New\tCtrl+N");
    add(file, Open, L"&Open…\tCtrl+O");
    add(file, OpenFolder, L"Open &folder…");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    add(file, Save, L"&Save\tCtrl+S");
    add(file, SaveAs, L"Save &as…\tCtrl+Shift+S");
    add(file, SaveAll, L"Save all\tCtrl+Shift+Alt+S");
    add(file, Reload, L"Reload from disk…");
    auto enc = CreatePopupMenu();
    add(enc, Enc8, L"UTF-8");
    add(enc, Enc8Bom, L"UTF-8 with BOM");
    add(enc, Enc16LE, L"UTF-16 LE (BOM)");
    add(enc, Enc16BE, L"UTF-16 BE (BOM)");
    add_menu(file, UINT_PTR(enc), L"Save &encoding", MF_POPUP);
    auto eol = CreatePopupMenu();
    add(eol, EolLF, L"Convert to LF");
    add(eol, EolCRLF, L"Convert to CRLF");
    add(eol, EolCR, L"Convert to CR");
    add_menu(file, UINT_PTR(eol), L"Line &endings", MF_POPUP);
    add(file, Recent, L"Recent files…");
    add(file, ClearRecent, L"Clear recent file history");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    add(file, Close, L"&Close tab\tCtrl+W");
    add(file, CloseOthers, L"Close other tabs…");
    add(file, Exit, L"E&xit\tAlt+F4");
    add(edit, Undo, L"&Undo\tCtrl+Z");
    add(edit, Redo, L"&Redo\tCtrl+Y");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    add(edit, Cut, L"Cu&t\tCtrl+X");
    add(edit, Copy, L"&Copy\tCtrl+C");
    add(edit, Paste, L"&Paste\tCtrl+V");
    add(edit, SelectAll, L"Select &all\tCtrl+A");
    add(view, Wrap, L"Word &wrap");
    add(view, Lines, L"&Line numbers");
    add(view, Whitespace, L"Show whitespace");
    add(view, Sidebar, L"&Explorer sidebar");
    add(view, Preview, L"Markdown &reading pane");
    add(view, Focus, L"&Focus mode\tF11");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    add(view, Dark, L"&Dark theme");
    add(view, Light, L"Li&ght theme");
    add(view, System, L"&System theme");
    add(view, Font, L"Editor &font…");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    add(view, ZoomIn, L"Zoom in\tCtrl++");
    add(view, ZoomOut, L"Zoom out\tCtrl+-");
    add(view, ZoomReset, L"Reset zoom\tCtrl+0");
    add(search, Find, L"&Find…\tCtrl+F");
    add(search, Replace, L"&Replace…\tCtrl+H");
    add(search, FindNext, L"Find &next\tF3");
    add(search, FindPrev, L"Find &previous\tShift+F3");
    add(search, GoTo, L"&Go to line / column…\tCtrl+G");
    add(tools, Palette, L"Command &palette…\tCtrl+Shift+P");
    add(tools, Recovery, L"Unsaved-buffer recovery");
    add(tools, History, L"Remember recent files");
    add(tools, RestoreSession, L"Reopen clean tabs on launch");
    add(tools, ClearRecovery, L"Clear stored recovery…");
    add(windows, NextTab, L"Next tab\tCtrl+Tab");
    add(windows, PrevTab, L"Previous tab\tCtrl+Shift+Tab");
    add(windows, MoveLeft, L"Move tab left");
    add(windows, MoveRight, L"Move tab right");
    add(help, Help, L"Offline &help");
    add(help, About, L"&About Indic NotePad");
    for (auto pair : std::vector<std::pair<HMENU, const wchar_t*>>{{file, L"&File"},
                                                                   {edit, L"&Edit"},
                                                                   {view, L"&View"},
                                                                   {search, L"&Search"},
                                                                   {tools, L"&Tools"},
                                                                   {windows, L"&Window"},
                                                                   {help, L"&Help"}})
        add_menu(root, UINT_PTR(pair.first), pair.second, MF_POPUP);
    return root;
}
void App::apply_theme() {
    HIGHCONTRASTW hc{sizeof(hc)};
    SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0);
    high_contrast = (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
    DWORD light = 1, size = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER,
                 L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    dark = theme == 0 || (theme == 2 && !light);
    if (high_contrast)
        c = {GetSysColor(COLOR_WINDOW),     GetSysColor(COLOR_BTNFACE),
             GetSysColor(COLOR_WINDOW),     GetSysColor(COLOR_WINDOWTEXT),
             GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_WINDOWTEXT),
             GetSysColor(COLOR_HIGHLIGHT),  GetSysColor(COLOR_HIGHLIGHT),
             GetSysColor(COLOR_WINDOWTEXT)};
    else if (dark)
        c = {RGB(12, 20, 31),    RGB(18, 29, 43),    RGB(14, 23, 35),
             RGB(224, 232, 244), RGB(157, 176, 201), RGB(38, 55, 74),
             RGB(79, 168, 255),  RGB(31, 55, 79),    RGB(101, 218, 190)};
    else
        c = {RGB(242, 246, 252), RGB(231, 238, 248), RGB(255, 255, 255),
             RGB(27, 42, 64),    RGB(78, 99, 125),   RGB(203, 215, 230),
             RGB(19, 98, 190),   RGB(210, 229, 253), RGB(12, 110, 86)};
    for (auto b : {bg_brush, panel_brush, editor_brush})
        if (b)
            DeleteObject(b);
    bg_brush = CreateSolidBrush(c.bg);
    panel_brush = CreateSolidBrush(c.panel);
    editor_brush = CreateSolidBrush(c.editor);
    for (auto f : {ui_font, small_font, bold_font, title_font})
        if (f)
            DeleteObject(f);
    auto font = [&](int points, int weight) {
        return CreateFontW(-MulDiv(points, dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    };
    ui_font = font(10, FW_NORMAL);
    small_font = font(9, FW_NORMAL);
    bold_font = font(10, FW_SEMIBOLD);
    title_font = font(17, FW_SEMIBOLD);
    BOOL use_dark = dark && !high_contrast;
    DwmSetWindowAttribute(window, 20, &use_dark, sizeof(use_dark));
    for (auto h : toolbar)
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
    for (auto h : search_buttons)
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
    for (auto h :
         {tabs, tree, reader, find_edit, replace_edit, case_check, word_check, regex_check})
        if (h) {
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
            SetWindowTheme(h, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
        }
    TreeView_SetBkColor(tree, c.panel);
    TreeView_SetTextColor(tree, c.text);
    TreeView_SetLineColor(tree, c.line);
    SendMessageW(reader, EM_SETBKGNDCOLOR, 0, c.editor);
    if (editor)
        style_editor();
    if (menubar) {
        MENUINFO info{sizeof(info)};
        info.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
        info.hbrBack = bg_brush;
        SetMenuInfo(menubar, &info);
    }
    DrawMenuBar(window);
    layout();
    invalidate();
}
std::string lexer_for(const fs::path& p) {
    auto ext = p.extension().wstring();
    CharLowerBuffW(ext.data(), DWORD(ext.size()));
    if (ext == L".py" || ext == L".pyw")
        return "python";
    if (ext == L".json" || ext == L".jsonc")
        return "json";
    if (ext == L".md" || ext == L".markdown")
        return "markdown";
    if (ext == L".html" || ext == L".htm" || ext == L".xml" || ext == L".svg")
        return "hypertext";
    if (ext == L".css")
        return "css";
    if (ext == L".sh" || ext == L".bash")
        return "bash";
    if (ext == L".ps1")
        return "powershell";
    if (ext == L".ini" || ext == L".toml" || ext == L".conf")
        return "props";
    if (ext == L".yml" || ext == L".yaml")
        return "yaml";
    if (ext == L".cpp" || ext == L".c" || ext == L".h" || ext == L".hpp" || ext == L".js" ||
        ext == L".ts" || ext == L".rs" || ext == L".java" || ext == L".cs")
        return "cpp";
    return "null";
}
void App::style_editor() {
    auto d = current();
    sci(SCI_STYLESETFORE, STYLE_DEFAULT, c.text);
    sci(SCI_STYLESETBACK, STYLE_DEFAULT, c.editor);
    auto family = utf8(font_name);
    sci(SCI_STYLESETFONT, STYLE_DEFAULT, reinterpret_cast<sptr_t>(family.c_str()));
    sci(SCI_STYLESETSIZE, STYLE_DEFAULT, font_size);
    sci(SCI_STYLECLEARALL);
    sci(SCI_STYLESETFORE, STYLE_LINENUMBER, c.muted);
    sci(SCI_STYLESETBACK, STYLE_LINENUMBER, c.editor);
    sci(SCI_SETMARGINWIDTHN, 0, numbers ? s(58) : 0);
    sci(SCI_SETMARGINLEFT, 0, s(16));
    sci(SCI_SETMARGINRIGHT, 0, s(16));
    sci(SCI_SETCARETFORE, c.text);
    sci(SCI_SETCARETWIDTH, 2);
    sci(SCI_SETCARETPERIOD, 0);
    sci(SCI_SETCARETLINEVISIBLE, 1);
    sci(SCI_SETCARETLINEBACK, c.panel);
    sci(SCI_SETSELFORE, 1, c.text);
    sci(SCI_SETSELBACK, 1, c.selected);
    sci(SCI_SETWHITESPACEFORE, 1, c.muted);
    sci(SCI_SETWRAPMODE, wrap && (!d || d->bytes <= 1024 * 1024) ? SC_WRAP_WORD : SC_WRAP_NONE);
    sci(SCI_SETVIEWWS, whitespace ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE);
    sci(SCI_SETZOOM, zoom);
    sci(SCI_SETINDENTATIONGUIDES, SC_IV_LOOKBOTH);
    sci(SCI_SETINDENT, 4);
    sci(SCI_SETTABWIDTH, 4);
    sci(SCI_SETUSETABS, 0);
    sci(SCI_SETSCROLLWIDTHTRACKING, 1);
    sci(SCI_SETSCROLLWIDTH, 1);
    sci(SCI_SETLAYOUTCACHE, SC_CACHE_PAGE);
    sci(SCI_SETPHASESDRAW, SC_PHASES_TWO);
    sci(SCI_SETFONTQUALITY, SC_EFF_QUALITY_LCD_OPTIMIZED);
    sci(SCI_SETAUTOMATICFOLD, 0);
    auto lexer = d ? lexer_for(d->path) : "null";
    if (d && d->truncated)
        lexer = "null";
    sci(SCI_SETILEXER, 0, reinterpret_cast<sptr_t>(MakeLexer(lexer.c_str())));
    auto set = [&](int style, COLORREF color, bool bold = false) {
        sci(SCI_STYLESETFORE, style, color);
        sci(SCI_STYLESETBOLD, style, bold);
    };
    auto green = dark ? RGB(123, 199, 153) : RGB(38, 115, 67),
         purple = dark ? RGB(192, 156, 241) : RGB(115, 55, 169),
         orange = dark ? RGB(231, 185, 116) : RGB(151, 87, 14);
    if (lexer == "cpp") {
        set(SCE_C_COMMENT, green);
        set(SCE_C_COMMENTLINE, green);
        set(SCE_C_COMMENTDOC, green);
        set(SCE_C_NUMBER, orange);
        set(SCE_C_WORD, purple, true);
        set(SCE_C_STRING, orange);
        set(SCE_C_CHARACTER, orange);
        set(SCE_C_PREPROCESSOR, c.accent);
        sci(SCI_SETKEYWORDS, 0,
            reinterpret_cast<sptr_t>(
                "auto bool break case catch char class const constexpr continue default delete do "
                "double else enum false float for if import int interface let long namespace new "
                "null nullptr private public return short static std string struct switch template "
                "this throw true try typedef typename unsigned using var virtual void while "
                "function export async await fn impl mut pub use"));
    } else if (lexer == "python") {
        set(SCE_P_COMMENTLINE, green);
        set(SCE_P_NUMBER, orange);
        set(SCE_P_STRING, orange);
        set(SCE_P_CHARACTER, orange);
        set(SCE_P_TRIPLE, orange);
        set(SCE_P_TRIPLEDOUBLE, orange);
        set(SCE_P_WORD, purple, true);
        set(SCE_P_DEFNAME, c.accent, true);
        sci(SCI_SETKEYWORDS, 0,
            reinterpret_cast<sptr_t>(
                "and as assert async await break class continue def del elif else except False "
                "finally for from global if import in is lambda None nonlocal not or pass raise "
                "return True try while with yield"));
    } else if (lexer == "json") {
        set(SCE_JSON_PROPERTYNAME, c.accent);
        set(SCE_JSON_STRING, green);
        set(SCE_JSON_NUMBER, orange);
        set(SCE_JSON_KEYWORD, purple);
    } else if (lexer == "markdown") {
        for (int n = SCE_MARKDOWN_HEADER1; n <= SCE_MARKDOWN_HEADER6; ++n)
            set(n, c.accent, true);
        set(SCE_MARKDOWN_STRONG1, purple, true);
        set(SCE_MARKDOWN_CODE, green);
        set(SCE_MARKDOWN_CODE2, green);
        set(SCE_MARKDOWN_CODEBK, green);
        set(SCE_MARKDOWN_LINK, orange);
    } else {
        for (int n : {1, 2})
            set(n, green);
        for (int n : {3, 4, 5, 6})
            set(n, c.accent);
        for (int n : {7, 8, 9})
            set(n, orange);
    }
    if (d) {
        sci(SCI_SETEOLMODE, eol_mode(d->eol));
        sci(SCI_SETREADONLY, d->read_only || busy);
    }
    sci(SCI_COLOURISE, 0, -1);
}
void App::update_tabs() {
    for (int n = 0; n < int(docs.size()); ++n) {
        auto& d = *docs[size_t(n)];
        auto name =
            d.title + (dirty(d) ? L"  ●" : L"") + (d.read_only ? L"  [read only]" : L"") + L"     ";
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = name.data();
        TabCtrl_SetItem(tabs, n, &item);
    }
    if (auto d = current()) {
        auto title = d->title + (dirty(*d) ? L" •" : L"") + L" — Indic NotePad";
        SetWindowTextW(window, title.c_str());
    }
    InvalidateRect(tabs, nullptr, FALSE);
}
void App::select(int n) {
    if (n < 0 || n >= int(docs.size()))
        return;
    loading = true;
    if (auto old = current()) {
        old->cursor = sci(SCI_GETCURRENTPOS);
        old->anchor = sci(SCI_GETANCHOR);
        old->first_line = sci(SCI_GETFIRSTVISIBLELINE);
    }
    active = n;
    auto& d = *docs[size_t(n)];
    sci(SCI_SETDOCPOINTER, 0, d.pointer);
    sci(SCI_SETSEL, d.anchor, d.cursor);
    sci(SCI_SETFIRSTVISIBLELINE, d.first_line);
    loading = false;
    TabCtrl_SetCurSel(tabs, n);
    style_editor();
    update_tabs();
    refresh_preview();
    SetFocus(editor);
    invalidate();
}
void App::add_document(Loaded value, std::wstring title) {
    if (docs.size() >= 50)
        throw Error("This profile supports at most 50 open tabs.");
    auto d = std::make_unique<Doc>();
    d->id = ++next_id;
    d->path = std::move(value.path);
    d->title = title.empty() ? (d->path.empty() ? L"Untitled " + std::to_wstring(d->id)
                                                : d->path.filename().wstring())
                             : std::move(title);
    d->encoding = value.encoding;
    d->eol = value.eol;
    d->stamp = value.stamp;
    d->read_only = value.read_only || read_only_arg;
    d->truncated = value.truncated;
    d->chars = characters(value.text);
    d->bytes = value.text.size();
    uint64_t total = d->bytes;
    for (auto& item : docs)
        total += item->bytes;
    if (total > 128ull * 1024 * 1024)
        throw Error(
            "Open buffers exceed the 128 MiB text budget. Save and close other tabs first.");
    d->pointer = sci(SCI_CREATEDOCUMENT, 0, 0);
    if (!d->pointer)
        throw Error("Cannot allocate editor document.");
    auto old = sci(SCI_GETDOCPOINTER);
    auto old_cursor = sci(SCI_GETCURRENTPOS), old_anchor = sci(SCI_GETANCHOR),
         old_scroll = sci(SCI_GETFIRSTVISIBLELINE);
    sci(SCI_ADDREFDOCUMENT, 0, old);
    loading = true;
    sci(SCI_SETDOCPOINTER, 0, d->pointer);
    sci(SCI_SETREADONLY, 0);
    sci(SCI_SETCODEPAGE, SC_CP_UTF8);
    sci(SCI_SETUNDOCOLLECTION, 0);
    sci(SCI_ADDTEXT, value.text.size(), reinterpret_cast<sptr_t>(value.text.data()));
    sci(SCI_EMPTYUNDOBUFFER);
    sci(SCI_SETUNDOCOLLECTION, 1);
    sci(SCI_SETSAVEPOINT);
    sci(SCI_SETDOCPOINTER, 0, old);
    sci(SCI_RELEASEDOCUMENT, 0, old);
    sci(SCI_SETSEL, old_anchor, old_cursor);
    sci(SCI_SETFIRSTVISIBLELINE, old_scroll);
    loading = false;
    docs.push_back(std::move(d));
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = docs.back()->title.data();
    TabCtrl_InsertItem(tabs, int(docs.size() - 1), &item);
    select(int(docs.size() - 1));
    if (value.truncated)
        status = L"Limited read-only prefix · full file is not loaded";
}
void App::new_document() {
    Loaded d;
    d.eol = Eol::lf;
    add_document(std::move(d));
    status = L"New document · UTF-8";
}
void App::begin_job(std::function<std::function<void()>()> work) {
    if (busy)
        throw Error("Another file/search operation is still finishing.");
    if (worker.joinable())
        worker.join();
    busy = true;
    cancel = false;
    invalidate();
    worker = std::jthread([this, work = std::move(work)]() {
        std::function<void()> callback;
        try {
            callback = work();
        } catch (const std::exception& e) {
            auto message = std::string(e.what());
            callback = [this, message] { error(message); };
        }
        auto heap = new std::function<void()>(std::move(callback));
        if (!PostMessageW(window, JOB_DONE, 0, reinterpret_cast<LPARAM>(heap)))
            delete heap;
    });
}
void App::open_file(fs::path path, bool preview_ok) {
    if (busy) {
        if (pending_files.size() < 50)
            pending_files.push_back(path);
        status = L"File queued after current operation";
        invalidate();
        return;
    }
    auto key = path_key(path);
    for (int n = 0; n < int(docs.size()); ++n)
        if (!docs[size_t(n)]->path.empty() && path_key(docs[size_t(n)]->path) == key) {
            select(n);
            return;
        }
    std::error_code ec;
    auto size = fs::file_size(path, ec);
    if (!ec && size > edit_limit && !preview_ok) {
        if (MessageBoxW(window,
                        L"This file exceeds the 32 MiB editing limit. Open a read-only preview of "
                        L"its first 4 MiB? The complete file will not be loaded or searchable, and "
                        L"the preview cannot be saved.",
                        L"Large file preview", MB_YESNO | MB_ICONINFORMATION) != IDYES)
            return;
        preview_ok = true;
    }
    status = L"Opening file…";
    begin_job([this, path, preview_ok] {
        auto loaded = load(path, preview_ok);
        return [this, loaded = std::move(loaded)]() mutable {
            auto p = loaded.path;
            for (int n = 0; n < int(docs.size()); ++n) {
                auto& existing = *docs[size_t(n)];
                if (existing.stamp.exists && existing.stamp.id == loaded.stamp.id &&
                    existing.stamp.volume == loaded.stamp.volume) {
                    select(n);
                    status = L"File is already open";
                    return;
                }
            }
            auto title = p.filename().wstring();
            bool limited = loaded.truncated;
            bool replace_blank = docs.size() == 1 && docs[0]->path.empty() && !dirty(*docs[0]) &&
                                 docs[0]->bytes == 0;
            add_document(std::move(loaded));
            if (replace_blank)
                close_doc(0);
            if (history) {
                std::erase_if(recent, [&](auto& r) { return path_key(r) == path_key(p); });
                recent.insert(recent.begin(), p);
                if (recent.size() > 12)
                    recent.resize(12);
                save_settings();
            }
            status = limited ? L"Read-only · first 4 MiB only" : L"Opened " + title;
            checkpoint();
        };
    });
}
void App::open_dialog() {
    wchar_t path[32768]{};
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = window;
    ofn.lpstrFile = path;
    ofn.nMaxFile = 32768;
    ofn.lpstrFilter = L"Text and source "
                      L"files\0*.txt;*.md;*.json;*.cpp;*.py;*.log;*.csv;*.ini;*.html;*.xml;*.js;*."
                      L"yaml\0All files\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn))
        open_file(path);
}
Stamp save_responsive(const fs::path& path, std::string_view text, Encoding enc,
                      const std::optional<Stamp>& expected, bool overwrite) {
    Handle done(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!done)
        throw Error("Cannot create save completion event.");
    std::optional<Stamp> result;
    std::exception_ptr failure;
    app.busy = true;
    app.status = L"Saving document…";
    app.sci(SCI_SETREADONLY, 1);
    app.invalidate();
    std::jthread io([&] {
        try {
            result = np::save(path, text, enc, expected, overwrite);
        } catch (...) {
            failure = std::current_exception();
        }
        SetEvent(done.h);
    });
    while (WaitForSingleObject(done.h, 0) == WAIT_TIMEOUT) {
        MsgWaitForMultipleObjects(1, &done.h, FALSE, INFINITE, QS_ALLINPUT);
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                PostQuitMessage(int(msg.wParam));
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    io.join();
    app.busy = false;
    if (auto d = app.current())
        app.sci(SCI_SETREADONLY, d->read_only);
    if (failure)
        std::rethrow_exception(failure);
    return *result;
}
bool App::save_doc(bool as) {
    auto d = current();
    if (!d)
        return true;
    if (busy) {
        status = L"Please wait for the current operation";
        invalidate();
        return false;
    }
    if (d->truncated) {
        error("A limited preview cannot be saved. It does not contain the full file.");
        return false;
    }
    fs::path path = d->path;
    bool save_as = as || path.empty() || d->read_only;
    bool overwrite = false;
    if (save_as) {
        std::wstring buffer(32768, L'\0');
        auto name = d->path.empty() ? d->title + L".txt" : display_path(d->path);
        std::copy_n(name.data(), std::min<size_t>(name.size(), 32767), buffer.data());
        OPENFILENAMEW ofn{sizeof(ofn)};
        ofn.hwndOwner = window;
        ofn.lpstrFile = buffer.data();
        ofn.nMaxFile = DWORD(buffer.size());
        ofn.lpstrFilter = L"Text files\0*.txt\0All files\0*.*\0";
        ofn.lpstrDefExt = L"txt";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&ofn))
            return false;
        path = buffer.c_str();
        for (auto& other : docs)
            if (other.get() != d && !other->path.empty() &&
                path_key(other->path) == path_key(path)) {
                error("That path is already open in another tab.");
                return false;
            }
        overwrite = true;
    }
    // File IO runs on a worker while the native message pump keeps paint/resize responsive.
    // Editing is temporarily frozen so the successful disk snapshot is exactly this buffer.
    try {
        auto contents = text();
        auto expected = (!save_as || (!d->path.empty() && path_key(path) == path_key(d->path)))
                            ? std::optional<Stamp>(d->stamp)
                            : std::nullopt;
        SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        auto stamp = save_responsive(path, contents, d->encoding, expected, overwrite);
        d->stamp = stamp;
        d->path = normalized(path);
        d->title = d->path.filename().wstring();
        d->metadata_dirty = false;
        d->text_dirty = false;
        d->read_only = false;
        sci(SCI_SETREADONLY, 0);
        sci(SCI_SETSAVEPOINT);
        d->eol = detect_eol(contents);
        style_editor();
        update_tabs();
        status = L"Saved safely · " + d->title;
        refresh_preview();
        schedule_recovery();
        return true;
    } catch (const std::exception& e) {
        error(e.what());
        return false;
    }
}
void App::save_all() {
    if (busy)
        return;
    int initial = active;
    for (int n = 0; n < int(docs.size()); ++n)
        if (dirty(*docs[size_t(n)])) {
            select(n);
            if (!save_doc())
                break;
        }
    select(initial);
}
bool App::close_doc(int n) {
    if (busy)
        return false;
    if (n < 0 || n >= int(docs.size()))
        return true;
    select(n);
    auto d = current();
    if (dirty(*d)) {
        auto title = L"Save changes to “" + d->title +
                     L"”?\n\nSave writes the document. Don't Save discards this buffer. Cancel "
                     L"keeps it open.";
        int answer =
            MessageBoxW(window, title.c_str(), L"Unsaved changes", MB_YESNOCANCEL | MB_ICONWARNING);
        if (answer == IDCANCEL)
            return false;
        if (answer == IDYES && !save_doc())
            return false;
    }
    loading = true;
    sci(SCI_SETDOCPOINTER, 0, 0);
    auto pointer = d->pointer;
    docs.erase(docs.begin() + n);
    TabCtrl_DeleteItem(tabs, n);
    sci(SCI_RELEASEDOCUMENT, 0, pointer);
    active = -1;
    loading = false;
    if (!docs.empty())
        select(std::min(n, int(docs.size() - 1)));
    else
        new_document();
    schedule_recovery();
    return true;
}
bool App::close_window() {
    if (busy) {
        cancel = true;
        status = L"Cancelling/waiting for operation. Close again when finished.";
        invalidate();
        return false;
    }
    std::vector<Snapshot> reopen;
    for (int n = 0; n < int(docs.size()); ++n) {
        select(n);
        auto d = current();
        if (dirty(*d)) {
            int answer =
                MessageBoxW(window,
                            (L"Save changes to “" + d->title +
                             L"”?\n\nYes: Save  ·  No: Don't Save  ·  Cancel: keep the app open")
                                .c_str(),
                            L"Unsaved changes", MB_YESNOCANCEL | MB_ICONWARNING);
            if (answer == IDCANCEL)
                return false;
            if (answer == IDYES && !save_doc())
                return false;
        }
        if (restore && !d->path.empty() && !d->truncated) {
            Snapshot entry;
            entry.path = d->path.wstring();
            entry.title = d->title;
            entry.cursor = uint64_t(sci(SCI_GETCURRENTPOS));
            entry.anchor = uint64_t(sci(SCI_GETANCHOR));
            entry.stamp = d->stamp;
            reopen.push_back(std::move(entry));
        }
    }
    try {
        if (benchmark_ms)
            write_atomic(data_dir / L"native-loop.json",
                         "{\"main_paints\":" + std::to_string(paint_count) +
                             ",\"scintilla_ui_updates\":" + std::to_string(update_count) +
                             ",\"app_timers\":" + std::to_string(app_timer_count) + "}\n");
        save_settings();
        if (recovery && !preserve_recovery)
            save_session(data_dir / L"session.dat", reopen);
    } catch (const std::exception& e) {
        error(e.what());
        return false;
    }
    quitting = true;
    return true;
}
void App::changed() {
    if (loading || !current())
        return;
    auto d = current();
    ++d->revision;
    update_tabs();
    schedule_recovery();
    if (preview)
        SetTimer(window, 2, 350, nullptr);
    invalidate();
}
void App::layout() {
    if (!window)
        return;
    RECT r{};
    GetClientRect(window, &r);
    int w = r.right, h = r.bottom;
    int header = focus ? 0 : s(72), bar = focus ? 0 : s(40), tab = focus ? 0 : s(39),
        status_h = s(32);
    editor_top = header + bar + tab;
    editor_bottom = std::max(editor_top + s(20), h - status_h);
    int left = sidebar && !focus && w > s(800) ? s(sidebar_width) : 0;
    int right = preview && !focus && w > s(940) ? s(preview_width) : 0;
    for (size_t n = 0; n < toolbar.size(); ++n) {
        ShowWindow(toolbar[n], focus ? SW_HIDE : SW_SHOW);
        MoveWindow(toolbar[n], s(18 + int(n) * 78), header + s(3), s(72), s(32), TRUE);
    }
    ShowWindow(tabs, focus ? SW_HIDE : SW_SHOW);
    MoveWindow(tabs, left, header + bar, w - left - right, tab, TRUE);
    int search_h = IsWindowVisible(search_panel) && !focus ? s(105) : 0;
    MoveWindow(search_panel, left, editor_top, w - left - right, search_h, TRUE);
    ShowWindow(search_panel, search_h ? SW_SHOW : SW_HIDE);
    int ex = left, ey = editor_top + search_h, ew = std::max(s(40), w - left - right),
        eh = std::max(s(20), editor_bottom - ey);
    MoveWindow(editor, ex, ey, ew, eh, TRUE);
    ShowWindow(tree, left ? SW_SHOW : SW_HIDE);
    MoveWindow(tree, s(10), editor_top + s(44), std::max(0, left - s(20)),
               std::max(0, editor_bottom - editor_top - s(54)), TRUE);
    ShowWindow(reader, right ? SW_SHOW : SW_HIDE);
    MoveWindow(reader, w - right + s(18), editor_top + s(49), std::max(0, right - s(36)),
               std::max(0, editor_bottom - editor_top - s(65)), TRUE);
    if (search_h) {
        int width = w - left - right;
        MoveWindow(find_edit, s(18), s(12), std::max(s(100), width - s(358)), s(28), TRUE);
        MoveWindow(replace_edit, s(18), s(51), std::max(s(100), width - s(358)), s(28), TRUE);
        int bx = std::max(s(120), width - s(328));
        int ids[] = {PrevButton, NextButton, ReplaceButton, ReplaceAllButton, CloseSearch};
        for (size_t n = 0; n < search_buttons.size(); ++n) {
            int y = n < 2 ? s(12) : s(51), x = bx + int(n % 2) * s(97);
            if (n == 4) {
                x = width - s(35);
                y = s(12);
            }
            MoveWindow(search_buttons[n], x, y, n == 4 ? s(24) : s(91), s(28), TRUE);
        }
        MoveWindow(case_check, width - s(320), s(84), s(105), s(20), TRUE);
        MoveWindow(word_check, width - s(210), s(84), s(110), s(20), TRUE);
        MoveWindow(regex_check, width - s(95), s(84), s(85), s(20), TRUE);
    }
    invalidate();
}
void App::paint() {
    ++paint_count;
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(window, &ps);
    RECT r{};
    GetClientRect(window, &r);
    auto memory = CreateCompatibleDC(dc);
    auto bitmap = CreateCompatibleBitmap(dc, std::max(1L, r.right), std::max(1L, r.bottom));
    auto old = SelectObject(memory, bitmap);
    fill(memory, r, c.bg);
    if (!focus) {
        RECT header{0, 0, r.right, s(72)};
        fill(memory, header, c.bg);
        DrawIconEx(memory, s(21), s(17), icon, s(38), s(38), 0, nullptr, DI_NORMAL);
        label(memory, L"Indic NotePad", {s(74), s(12), s(340), s(41)}, title_font, c.text);
        label(memory, L"A little space for your thoughts.", {s(76), s(42), s(430), s(64)},
              small_font, c.muted);
        if (r.right > s(780)) {
            label(memory, L"FROM INDICSTUDIO", {r.right - s(230), s(15), r.right - s(22), s(37)},
                  small_font, c.muted, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            label(memory, busy ? L"Working locally…" : L"Local files. Your control.",
                  {r.right - s(260), s(38), r.right - s(22), s(61)}, small_font, c.green,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
        fill(memory, {0, s(72), r.right, s(112)}, c.panel);
        line(memory, 0, s(111), r.right, s(111), c.line);
        line(memory, 0, editor_top, r.right, editor_top, c.line);
    }
    int left = IsWindowVisible(tree) ? s(sidebar_width) : 0,
        right = IsWindowVisible(reader) ? s(preview_width) : 0;
    if (left) {
        fill(memory, {0, editor_top, left, editor_bottom}, c.panel);
        label(memory, L"EXPLORER", {s(19), editor_top + s(10), left - s(12), editor_top + s(36)},
              bold_font, c.muted);
        line(memory, left - 1, editor_top, left - 1, editor_bottom, c.line);
    }
    if (right) {
        fill(memory, {r.right - right, editor_top, r.right, editor_bottom}, c.editor);
        line(memory, r.right - right, editor_top, r.right - right, editor_bottom, c.line);
        label(memory, L"READING PANE",
              {r.right - right + s(18), editor_top + s(10), r.right - s(15), editor_top + s(38)},
              bold_font, c.muted);
    }
    fill(memory, {0, r.bottom - s(32), r.right, r.bottom}, c.panel);
    line(memory, 0, r.bottom - s(32), r.right, r.bottom - s(32), c.line);
    label(memory, busy ? L"●  Working…" : L"●  " + status,
          {s(16), r.bottom - s(32), std::max<LONG>(s(160), r.right / 3), r.bottom}, small_font,
          busy ? c.accent : c.green);
    if (auto d = current()) {
        auto pos = sci(SCI_GETCURRENTPOS);
        auto line_no = sci(SCI_LINEFROMPOSITION, pos) + 1;
        auto col = sci(SCI_GETCOLUMN, pos) + 1;
        auto selection = std::abs(sci(SCI_GETSELECTIONEND) - sci(SCI_GETSELECTIONSTART));
        auto info = L"Ln " + std::to_wstring(line_no) + L", Col " + std::to_wstring(col) + L"    " +
                    std::to_wstring(sci(SCI_GETLINECOUNT)) + L" lines    " +
                    std::to_wstring(d->chars) + L" chars" +
                    (selection ? L"    " + std::to_wstring(selection) + L" selected bytes" : L"");
        if (r.right > s(900))
            label(memory, info, {r.right / 3, r.bottom - s(32), r.right - s(370), r.bottom},
                  small_font, c.muted, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        auto tail = encoding_name(d->encoding) + L"   " + eol_name(d->eol) + L"   " +
                    wide(lexer_for(d->path)) + L"   " + std::to_wstring(100 + zoom * 10) + L"%";
        label(memory, tail, {r.right - s(355), r.bottom - s(32), r.right - s(15), r.bottom},
              small_font, c.muted, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    }
    BitBlt(dc, 0, 0, r.right, r.bottom, memory, 0, 0, SRCCOPY);
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    EndPaint(window, &ps);
}
std::string rtf_escape(std::string_view s) {
    auto w = wide(s);
    std::string out;
    for (wchar_t c : w) {
        if (c == '\\' || c == '{' || c == '}') {
            out += '\\';
            out += char(c);
        } else if (c == '\n')
            out += "\\par ";
        else if (c == '\r') {
        } else if (c == '\t')
            out += "\\tab ";
        else if (c < 128)
            out += char(c);
        else
            out += "\\u" + std::to_string(int16_t(c)) + "?";
    }
    return out;
}
DWORD CALLBACK stream_rtf(DWORD_PTR cookie, LPBYTE buffer, LONG size, LONG* used) {
    auto pair = reinterpret_cast<std::pair<std::string_view, size_t>*>(cookie);
    auto n = std::min<size_t>(size, pair->first.size() - pair->second);
    memcpy(buffer, pair->first.data() + pair->second, n);
    pair->second += n;
    *used = LONG(n);
    return 0;
}
void App::refresh_preview() {
    if (!preview)
        return;
    if (!reader) {
        rich_module = LoadLibraryExW(L"Msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!rich_module)
            throw Error("Windows RichEdit is unavailable.");
        reader = CreateWindowExW(0, MSFTEDIT_CLASS, L"Reading pane",
                                 WS_CHILD | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY, 0,
                                 0, 100, 100, window, reinterpret_cast<HMENU>(ReaderPane), instance,
                                 nullptr);
        if (!reader)
            throw Error("Cannot create the native reading pane.");
        SendMessageW(reader, EM_SETEVENTMASK, 0, 0);
        SendMessageW(reader, EM_AUTOURLDETECT, 0, 0);
        SendMessageW(reader, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
        SetWindowTheme(reader, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
        layout();
    }
    auto d = current();
    if (!d)
        return;
    auto contents = text();
    if (contents.size() > 256 * 1024) {
        SetWindowTextW(
            reader,
            L"Reading pane is limited to 256 KiB.\n\nUse the main editor for larger files.");
        return;
    }
    auto col = [](COLORREF c) {
        return "\\red" + std::to_string(GetRValue(c)) + "\\green" + std::to_string(GetGValue(c)) +
               "\\blue" + std::to_string(GetBValue(c)) + ";";
    };
    std::string rtf =
        "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}{\\f1 Consolas;}}{\\colortbl;" +
        col(c.text) + col(c.accent) + col(c.muted) + "}\\cf1\\f0\\fs22\\sa160 ";
    bool markdown = lexer_for(d->path) == "markdown" || d->title.ends_with(L".md");
    if (!markdown)
        rtf += "\\cf3\\fs19 Document properties\\par \\cf1\\fs22 " + rtf_escape(utf8(d->title)) +
               "\\par " + rtf_escape(utf8(encoding_name(d->encoding))) + "\\par " +
               std::to_string(d->chars) + " Unicode scalars\\par " +
               (d->truncated ? "Limited preview, not the full file" : "Editable local document") +
               "\\par \\cf3 Reading preview\\par \\cf1 ";
    bool code = false;
    std::istringstream lines_stream(contents);
    std::string row;
    while (std::getline(lines_stream, row)) {
        if (markdown && row.starts_with("```")) {
            code = !code;
            rtf += "\\par ";
            continue;
        }
        if (code)
            rtf += "\\f1\\fs20 " + rtf_escape(row) + "\\par ";
        else if (markdown && row.starts_with("#")) {
            size_t count = row.find_first_not_of('#');
            if (count == std::string::npos)
                count = 1;
            rtf += "\\f0\\b\\cf2\\fs" + std::to_string(count == 1 ? 34 : 27) + " " +
                   rtf_escape(row.substr(std::min(count + 1, row.size()))) +
                   "\\b0\\cf1\\fs22\\par ";
        } else {
            if (markdown && (row.starts_with("- ") || row.starts_with("* ")))
                row = "•  " + row.substr(2);
            rtf += "\\f0\\cf1\\fs22 " + rtf_escape(row) + "\\par ";
        }
    }
    rtf += '}';
    std::pair<std::string_view, size_t> data{rtf, 0};
    EDITSTREAM stream{reinterpret_cast<DWORD_PTR>(&data), 0, stream_rtf};
    SendMessageW(reader, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&stream));
    SendMessageW(reader, EM_SETBKGNDCOLOR, 0, c.editor);
    SendMessageW(reader, EM_SETSEL, 0, 0);
}
void App::show_search(bool) {
    ShowWindow(search_panel, SW_SHOW);
    focus = false;
    layout();
    SetFocus(find_edit);
    SendMessageW(find_edit, EM_SETSEL, 0, -1);
}
void App::find(bool previous, int operation) {
    auto d = current();
    if (!d || busy)
        return;
    if (!IsWindowVisible(search_panel))
        show_search(false);
    auto pattern = utf8(get_text(find_edit));
    if (pattern.empty()) {
        SetFocus(find_edit);
        return;
    }
    if (operation && d->read_only) {
        error("Read-only documents cannot be replaced.");
        return;
    }
    auto replacement = utf8(get_text(replace_edit));
    SearchOptions options{Button_GetCheck(case_check) == BST_CHECKED,
                          Button_GetCheck(word_check) == BST_CHECKED,
                          Button_GetCheck(regex_check) == BST_CHECKED};
    auto contents = text();
    auto id = d->id, revision = d->revision;
    auto start = size_t(sci(previous ? SCI_GETSELECTIONSTART : SCI_GETSELECTIONEND));
    auto selected_start = size_t(sci(SCI_GETSELECTIONSTART)),
         selected_end = size_t(sci(SCI_GETSELECTIONEND));
    last_pattern = pattern;
    last_replacement = replacement;
    last_options = options;
    status = L"Searching locally…";
    begin_job([this, contents = std::move(contents), pattern, replacement, options, id, revision,
               start, selected_start, selected_end, previous, operation] {
        auto matches = np::search(contents, pattern, options, &cancel);
        return [this, matches = std::move(matches), replacement, id, revision, start,
                selected_start, selected_end, previous, operation] {
            auto d = current();
            if (!d || d->id != id || d->revision != revision) {
                status = L"Search cancelled: document changed";
                return;
            }
            if (matches.empty()) {
                status = L"No matches found";
                return;
            }
            if (operation == 2) {
                auto message =
                    L"Replace all " + std::to_wstring(matches.size()) +
                    L" matches?\n\nReplacement is literal text. This is a single undo action.";
                if (MessageBoxW(window, message.c_str(), L"Replace All",
                                MB_YESNO | MB_ICONQUESTION) != IDYES)
                    return;
                uint64_t projected = sci(SCI_GETLENGTH);
                for (auto m : matches) {
                    projected -= m.end - m.start;
                    projected += replacement.size();
                    if (projected > edit_limit) {
                        error("Replacement exceeds the document limit. No edits applied.");
                        return;
                    }
                }
                sci(SCI_BEGINUNDOACTION);
                for (auto it = matches.rbegin(); it != matches.rend(); ++it) {
                    sci(SCI_SETTARGETSTART, it->start);
                    sci(SCI_SETTARGETEND, it->end);
                    sci(SCI_REPLACETARGET, replacement.size(),
                        reinterpret_cast<sptr_t>(replacement.data()));
                }
                sci(SCI_ENDUNDOACTION);
                status = L"Replaced " + std::to_wstring(matches.size()) + L" matches";
                return;
            }
            if (operation == 1) {
                auto it = std::find_if(matches.begin(), matches.end(), [&](auto m) {
                    return m.start == selected_start && m.end == selected_end;
                });
                if (it != matches.end()) {
                    if (uint64_t(sci(SCI_GETLENGTH)) - (it->end - it->start) + replacement.size() >
                        edit_limit) {
                        error("Replacement exceeds the document limit.");
                        return;
                    }
                    sci(SCI_SETTARGETSTART, it->start);
                    sci(SCI_SETTARGETEND, it->end);
                    sci(SCI_REPLACETARGET, replacement.size(),
                        reinterpret_cast<sptr_t>(replacement.data()));
                    sci(SCI_SETSEL, it->start, it->start + replacement.size());
                    status = L"Selection replaced";
                    return;
                }
            }
            auto pick = matches.begin();
            if (previous) {
                pick = matches.end() - 1;
                for (auto it = matches.begin(); it != matches.end(); ++it)
                    if (it->start < start)
                        pick = it;
                    else
                        break;
            } else {
                auto it = std::find_if(matches.begin(), matches.end(),
                                       [&](auto m) { return m.start >= start; });
                if (it != matches.end())
                    pick = it;
            }
            sci(SCI_SETSEL, pick->start, pick->end);
            sci(SCI_SCROLLCARET);
            status = std::to_wstring(matches.size()) + L" matches · wraps around";
            SetFocus(editor);
        };
    });
}
void App::schedule_recovery() {
    if (recovery && !preserve_recovery)
        SetTimer(window, 1, 1800, nullptr);
}
void App::checkpoint() {
    KillTimer(window, 1);
    if (!recovery || preserve_recovery)
        return;
    if (busy) {
        deferred_recovery = true;
        return;
    }
    std::vector<Snapshot> snapshots;
    uint64_t total = 0;
    for (auto& item : docs) {
        auto& d = *item;
        if (d.truncated)
            continue;
        Snapshot snap;
        snap.path = d.path.wstring();
        snap.title = d.title;
        snap.encoding = d.encoding;
        snap.eol = d.eol;
        snap.dirty = dirty(d);
        snap.stamp = d.stamp;
        if (snap.dirty) {
            snap.text = doc_text(d);
            total += snap.text.size();
            if (total > session_limit - 1024 * 1024) {
                status = L"Recovery limit reached · save your documents";
                invalidate();
                return;
            }
        }
        if (&d == current()) {
            snap.cursor = uint64_t(sci(SCI_GETCURRENTPOS));
            snap.anchor = uint64_t(sci(SCI_GETANCHOR));
        } else {
            snap.cursor = uint64_t(d.cursor);
            snap.anchor = uint64_t(d.anchor);
        }
        if (!snap.dirty) {
            snap.cursor = 0;
            snap.anchor = 0;
        }
        if (snap.dirty || restore && !snap.path.empty())
            snapshots.push_back(std::move(snap));
    }
    auto previous = status;
    begin_job([this, snapshots = std::move(snapshots), previous] {
        try {
            save_session(data_dir / L"session.dat", snapshots);
            return std::function<void()>([this, previous] { status = previous; });
        } catch (const std::exception&) {
            return std::function<void()>(
                [this] { status = L"Recovery write failed · save your documents now"; });
        }
    });
}
void App::load_settings() {
    auto path = data_dir / L"settings.ini";
    if (!fs::exists(path))
        return;
    std::istringstream rows(read_bytes(path, 64 * 1024));
    std::string row;
    while (std::getline(rows, row)) {
        if (!row.empty() && row.back() == '\r')
            row.pop_back();
        auto at = row.find('=');
        if (at == std::string::npos)
            continue;
        auto key = row.substr(0, at), v = row.substr(at + 1);
        try {
            if (key == "theme")
                theme = std::clamp(std::stoi(v), 0, 2);
            else if (key == "wrap")
                wrap = v == "1";
            else if (key == "numbers")
                numbers = v == "1";
            else if (key == "recovery")
                recovery = v == "1";
            else if (key == "restore")
                restore = v == "1";
            else if (key == "history")
                history = v == "1";
            else if (key == "sidebar")
                sidebar = v == "1";
            else if (key == "preview")
                preview = v == "1";
            else if (key == "sidebar_width")
                sidebar_width = std::clamp(std::stoi(v), 160, 360);
            else if (key == "preview_width")
                preview_width = std::clamp(std::stoi(v), 240, 480);
            else if (key == "font_size")
                font_size = std::clamp(std::stoi(v), 8, 30);
            else if (key == "font")
                font_name = wide(v);
            else if (key == "recent" && recent.size() < 12)
                recent.emplace_back(wide(v));
        } catch (...) {
        }
    }
}
void App::save_settings() {
    std::string out =
        "version=1\ntheme=" + std::to_string(theme) + "\nwrap=" + std::to_string(wrap) +
        "\nnumbers=" + std::to_string(numbers) + "\nrecovery=" + std::to_string(recovery) +
        "\nhistory=" + std::to_string(history) + "\nrestore=" + std::to_string(restore) +
        "\nsidebar=" + std::to_string(sidebar) + "\npreview=" + std::to_string(preview) +
        "\nsidebar_width=" + std::to_string(sidebar_width) +
        "\npreview_width=" + std::to_string(preview_width) +
        "\nfont_size=" + std::to_string(font_size) + "\nfont=" + utf8(font_name) + "\n";
    if (history)
        for (auto& p : recent) {
            auto name = utf8(p.wstring());
            if (name.find_first_of("\n\r") == std::string::npos)
                out += "recent=" + name + "\n";
        }
    write_atomic(data_dir / L"settings.ini", out);
}
void App::restore_session() {
    auto path = data_dir / L"session.dat";
    if (!fs::exists(path) || !recovery)
        return;
    try {
        auto entries = read_session(path);
        if (entries.empty())
            return;
        bool any_dirty =
            std::any_of(entries.begin(), entries.end(), [](const auto& d) { return d.dirty; });
        if (any_dirty) {
            auto answer =
                MessageBoxW(window,
                            L"Saved recovery buffers are available.\n\nYes: restore them now.\nNo: "
                            L"discard the stored buffers.\nCancel: keep them for a later launch "
                            L"(no new recovery will overwrite them this session).",
                            L"Recover your work", MB_YESNOCANCEL | MB_ICONQUESTION);
            if (answer == IDCANCEL) {
                preserve_recovery = true;
                status = L"Previous recovery kept · new recovery paused";
                return;
            }
            if (answer == IDNO) {
                save_session(path, {});
                return;
            }
        }
        for (auto& entry : entries) {
            if (!entry.dirty && !restore)
                continue;
            try {
                if (!entry.dirty) {
                    if (!entry.path.empty() && fs::exists(entry.path))
                        add_document(load(entry.path));
                    continue;
                }
                Loaded d;
                d.path = entry.path;
                d.text = entry.text;
                d.encoding = entry.encoding;
                d.eol = entry.eol;
                d.stamp = entry.stamp;
                add_document(std::move(d), entry.title);
                auto doc = current();
                doc->metadata_dirty = true;
                sci(SCI_SETSEL, entry.anchor, entry.cursor);
            } catch (const std::exception& e) {
                status = L"Some saved tabs need attention";
                error(e.what());
            }
        }
        update_tabs();
    } catch (const std::exception& e) {
        preserve_recovery = true;
        error(e.what());
    }
}
void App::populate_tree(HTREEITEM parent, const fs::path& path) {
    std::vector<fs::directory_entry> items;
    std::error_code ec;
    for (auto it = fs::directory_iterator(path, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (items.size() >= 2000)
            break;
        auto a = GetFileAttributesW(it->path().c_str());
        if (a != INVALID_FILE_ATTRIBUTES &&
            (a & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_REPARSE_POINT)))
            continue;
        items.push_back(*it);
    }
    std::sort(items.begin(), items.end(), [](auto& a, auto& b) {
        bool ad = a.is_directory(), bd = b.is_directory();
        return ad != bd ? ad : a.path().filename() < b.path().filename();
    });
    for (auto& e : items) {
        auto title = e.path().filename().wstring();
        bool directory = e.is_directory(ec);
        if (directory)
            title = L"▸ " + title;
        TVINSERTSTRUCTW insert{};
        insert.hParent = parent;
        insert.hInsertAfter = TVI_LAST;
        insert.item.mask = TVIF_TEXT | TVIF_CHILDREN;
        insert.item.pszText = title.data();
        insert.item.cChildren = directory ? 1 : 0;
        auto handle = TreeView_InsertItem(tree, &insert);
        tree_paths.push_back({handle, e.path()});
    }
    if (items.size() >= 2000)
        status = L"Explorer shows at most 2,000 items per folder";
}
void App::browse_folder() {
    IFileDialog* dialog{};
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog))))
        return;
    DWORD options{};
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(L"Choose a workspace folder");
    if (SUCCEEDED(dialog->Show(window))) {
        IShellItem* item{};
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path{};
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                workspace = path;
                CoTaskMemFree(path);
                tree_paths.clear();
                TreeView_DeleteAllItems(tree);
                populate_tree(TVI_ROOT, workspace);
                sidebar = true;
                layout();
            }
            item->Release();
        }
    }
    dialog->Release();
}
void App::change_encoding(Encoding enc) {
    auto d = current();
    if (!d || d->read_only)
        return;
    if (d->encoding == enc)
        return;
    if (MessageBoxW(window,
                    L"Change the encoding used on the next Save? The in-memory Unicode text is "
                    L"unchanged. UTF-16 output includes a BOM.",
                    L"Save encoding", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    d->encoding = enc;
    d->metadata_dirty = true;
    update_tabs();
    schedule_recovery();
    invalidate();
}
void App::change_eol(Eol e) {
    auto d = current();
    if (!d || d->read_only)
        return;
    sci(SCI_BEGINUNDOACTION);
    sci(SCI_CONVERTEOLS, eol_mode(e));
    sci(SCI_ENDUNDOACTION);
    sci(SCI_SETEOLMODE, eol_mode(e));
    d->eol = e;
    schedule_recovery();
    invalidate();
}
struct Modal {
    bool palette{};
    HWND window{}, edit{}, list{};
    std::wstring value;
    int chosen{};
    bool done{};
    std::vector<int> ids;
    void filter() {
        auto query = get_text(edit);
        CharLowerBuffW(query.data(), DWORD(query.size()));
        SendMessageW(list, LB_RESETCONTENT, 0, 0);
        ids.clear();
        for (auto& [id, name] : app.palette_commands) {
            auto lower = name;
            CharLowerBuffW(lower.data(), DWORD(lower.size()));
            if (lower.find(query) != std::wstring::npos) {
                SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
                ids.push_back(id);
            }
        }
        SendMessageW(list, LB_SETCURSEL, 0, 0);
    }
};
LRESULT CALLBACK modal_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    auto m = reinterpret_cast<Modal*>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        m = static_cast<Modal*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(m));
        m->window = w;
        BOOL dark = app.dark && !app.high_contrast;
        DwmSetWindowAttribute(w, 20, &dark, sizeof(dark));
    }
    if (!m)
        return DefWindowProcW(w, msg, wp, lp);
    switch (msg) {
    case WM_CREATE: {
        m->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", m->value.c_str(),
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, app.s(20),
                                  app.s(20), app.s(520), app.s(32), w,
                                  reinterpret_cast<HMENU>(PaletteEdit), app.instance, nullptr);
        SendMessageW(m->edit, WM_SETFONT, reinterpret_cast<WPARAM>(app.ui_font), TRUE);
        SendMessageW(m->edit, EM_SETLIMITTEXT, 4096, 0);
        if (m->palette) {
            m->list = CreateWindowExW(0, L"LISTBOX", nullptr,
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY |
                                          LBS_NOINTEGRALHEIGHT,
                                      app.s(20), app.s(65), app.s(520), app.s(280), w,
                                      reinterpret_cast<HMENU>(PaletteList), app.instance, nullptr);
            SendMessageW(m->list, WM_SETFONT, reinterpret_cast<WPARAM>(app.ui_font), TRUE);
            SendMessageW(m->list, LB_SETITEMHEIGHT, 0, app.s(30));
            m->filter();
        }
        button(w, IDOK, m->palette ? L"Run command" : L"Go");
        MoveWindow(GetDlgItem(w, IDOK), app.s(325), app.s(m->palette ? 360 : 70), app.s(112),
                   app.s(32), TRUE);
        button(w, IDCANCEL, L"Cancel");
        MoveWindow(GetDlgItem(w, IDCANCEL), app.s(445), app.s(m->palette ? 360 : 70), app.s(95),
                   app.s(32), TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == PaletteEdit && HIWORD(wp) == EN_CHANGE && m->palette)
            m->filter();
        else if (LOWORD(wp) == IDCANCEL) {
            m->done = true;
            ShowWindow(w, SW_HIDE);
        } else if (LOWORD(wp) == IDOK || (LOWORD(wp) == PaletteList && HIWORD(wp) == LBN_DBLCLK)) {
            m->value = get_text(m->edit);
            if (m->palette) {
                auto n = int(SendMessageW(m->list, LB_GETCURSEL, 0, 0));
                if (n >= 0 && n < int(m->ids.size()))
                    m->chosen = m->ids[size_t(n)];
            } else
                m->chosen = 1;
            m->done = true;
            ShowWindow(w, SW_HIDE);
        }
        return 0;
    case WM_CLOSE:
        m->done = true;
        ShowWindow(w, SW_HIDE);
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetTextColor(reinterpret_cast<HDC>(wp), app.c.text);
        SetBkColor(reinterpret_cast<HDC>(wp), app.c.editor);
        return reinterpret_cast<LRESULT>(app.editor_brush);
    case WM_ERASEBKGND: {
        RECT r{};
        GetClientRect(w, &r);
        FillRect(reinterpret_cast<HDC>(wp), &r, app.bg_brush);
        return 1;
    }
    case WM_DRAWITEM:
        SendMessageW(app.window, msg, wp, lp);
        return TRUE;
    }
    return DefWindowProcW(w, msg, wp, lp);
}
void modal_run(Modal& m, const wchar_t* title) {
    RECT r{};
    GetWindowRect(app.window, &r);
    int width = app.s(580), height = app.s(m.palette ? 452 : 175);
    auto w = CreateWindowExW(WS_EX_DLGMODALFRAME, L"IndicNotePadModal", title,
                             WS_POPUP | WS_CAPTION | WS_SYSMENU, (r.left + r.right - width) / 2,
                             (r.top + r.bottom - height) / 2, width, height, app.window, nullptr,
                             app.instance, &m);
    if (!w)
        return;
    EnableWindow(app.window, FALSE);
    ShowWindow(w, SW_SHOW);
    SetFocus(m.edit);
    SendMessageW(m.edit, EM_SETSEL, 0, -1);
    MSG msg{};
    while (!m.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            SendMessageW(w, WM_COMMAND, IDCANCEL, 0);
            continue;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            SendMessageW(w, WM_COMMAND, IDOK, 0);
            continue;
        }
        if (m.palette && msg.message == WM_KEYDOWN &&
            (msg.wParam == VK_DOWN || msg.wParam == VK_UP) && msg.hwnd == m.edit) {
            auto n = int(SendMessageW(m.list, LB_GETCURSEL, 0, 0));
            n += msg.wParam == VK_DOWN ? 1 : -1;
            n = std::clamp(n, 0, std::max(0, int(m.ids.size()) - 1));
            SendMessageW(m.list, LB_SETCURSEL, n, 0);
            continue;
        }
        if (!IsDialogMessageW(w, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    DestroyWindow(w);
    EnableWindow(app.window, TRUE);
    SetActiveWindow(app.window);
    SetFocus(app.editor);
}
void App::show_palette() {
    Modal m;
    m.palette = true;
    modal_run(m, L"Commands · type to filter");
    if (m.chosen)
        command(m.chosen);
}
void App::show_goto() {
    Modal m;
    m.value = std::to_wstring(sci(SCI_LINEFROMPOSITION, sci(SCI_GETCURRENTPOS)) + 1) + L":1";
    modal_run(m, L"Go to line:column");
    if (!m.chosen)
        return;
    try {
        auto colon = m.value.find(':');
        size_t used{};
        int row = std::stoi(m.value.substr(0, colon), &used);
        if (used != (colon == std::wstring::npos ? m.value.size() : colon))
            throw Error("Enter line or line:column.");
        int col = 1;
        if (colon != std::wstring::npos) {
            auto part = m.value.substr(colon + 1);
            col = std::stoi(part, &used);
            if (used != part.size())
                throw Error("Invalid column.");
        }
        if (row < 1 || row > sci(SCI_GETLINECOUNT) || col < 1)
            throw Error("That line or column is outside the document.");
        auto begin = sci(SCI_POSITIONFROMLINE, row - 1), end = sci(SCI_GETLINEENDPOSITION, row - 1);
        auto position = sci(SCI_FINDCOLUMN, row - 1, col - 1);
        position = std::clamp(position, begin, end);
        sci(SCI_GOTOPOS, position);
        sci(SCI_SCROLLCARET);
    } catch (const std::exception& e) {
        error(e.what());
    }
}
void App::command(int id) {
    if (busy) {
        if (id == CloseSearch) {
            cancel = true;
            status = L"Cancelling search…";
        }
        invalidate();
        return;
    }
    auto d = current();
    switch (id) {
    case New:
        new_document();
        checkpoint();
        break;
    case Open:
        open_dialog();
        break;
    case OpenFolder:
        browse_folder();
        break;
    case Save:
        save_doc();
        break;
    case SaveAs:
        save_doc(true);
        break;
    case SaveAll:
        save_all();
        break;
    case Close:
        close_doc(active);
        break;
    case CloseOthers: {
        auto keep = d ? d->id : 0;
        for (int n = int(docs.size()) - 1; n >= 0; --n)
            if (docs[size_t(n)]->id != keep && !close_doc(n))
                break;
        break;
    }
    case Exit:
        SendMessageW(window, WM_CLOSE, 0, 0);
        break;
    case Reload:
        if (d && !d->path.empty() && !d->truncated) {
            if (MessageBoxW(
                    window,
                    L"Reload this file from disk? Unsaved buffer changes will be discarded.",
                    L"Reload document", MB_YESNO | MB_ICONWARNING) == IDYES) {
                auto value = load(d->path);
                loading = true;
                sci(SCI_SETREADONLY, 0);
                sci(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(value.text.c_str()));
                sci(SCI_EMPTYUNDOBUFFER);
                sci(SCI_SETSAVEPOINT);
                d->metadata_dirty = false;
                d->text_dirty = false;
                d->encoding = value.encoding;
                d->eol = value.eol;
                d->stamp = value.stamp;
                d->read_only = value.read_only;
                d->chars = characters(value.text);
                d->bytes = value.text.size();
                d->undo_bytes = 0;
                ++d->revision;
                loading = false;
                style_editor();
                update_tabs();
                refresh_preview();
                checkpoint();
            }
        }
        break;
    case Undo:
        sci(SCI_UNDO);
        break;
    case Redo:
        sci(SCI_REDO);
        break;
    case Cut:
        sci(SCI_CUT);
        break;
    case Copy:
        sci(SCI_COPY);
        break;
    case Paste:
        sci(SCI_PASTE);
        break;
    case SelectAll:
        sci(SCI_SELECTALL);
        break;
    case Find:
        show_search(false);
        break;
    case Replace:
        show_search(true);
        break;
    case FindNext:
    case NextButton:
        find();
        break;
    case FindPrev:
    case PrevButton:
        find(true);
        break;
    case ReplaceButton:
        find(false, 1);
        break;
    case ReplaceAllButton:
        find(false, 2);
        break;
    case CloseSearch:
        ShowWindow(search_panel, SW_HIDE);
        layout();
        SetFocus(editor);
        break;
    case GoTo:
        show_goto();
        break;
    case Wrap:
        wrap = !wrap;
        style_editor();
        break;
    case Lines:
        numbers = !numbers;
        style_editor();
        break;
    case Whitespace:
        whitespace = !whitespace;
        style_editor();
        break;
    case ZoomIn:
        zoom = std::min(12, zoom + 1);
        sci(SCI_SETZOOM, zoom);
        break;
    case ZoomOut:
        zoom = std::max(-5, zoom - 1);
        sci(SCI_SETZOOM, zoom);
        break;
    case ZoomReset:
        zoom = 0;
        sci(SCI_SETZOOM, 0);
        break;
    case Dark:
        theme = 0;
        apply_theme();
        refresh_preview();
        break;
    case Light:
        theme = 1;
        apply_theme();
        refresh_preview();
        break;
    case System:
        theme = 2;
        apply_theme();
        refresh_preview();
        break;
    case Sidebar:
        sidebar = !sidebar;
        if (sidebar && tree_paths.empty()) {
            TVINSERTSTRUCTW insert{};
            insert.hParent = TVI_ROOT;
            insert.hInsertAfter = TVI_LAST;
            insert.item.mask = TVIF_TEXT;
            insert.item.pszText = const_cast<wchar_t*>(L"Open a folder from File…");
            TreeView_InsertItem(tree, &insert);
        }
        layout();
        break;
    case Preview:
        preview = !preview;
        refresh_preview();
        layout();
        break;
    case Focus:
        focus = !focus;
        SetMenu(window, focus ? nullptr : menubar);
        DrawMenuBar(window);
        if (focus)
            ShowWindow(search_panel, SW_HIDE);
        layout();
        break;
    case Font: {
        LOGFONTW lf{};
        lf.lfHeight = -MulDiv(font_size, dpi, 72);
        wcsncpy_s(lf.lfFaceName, font_name.c_str(), LF_FACESIZE - 1);
        CHOOSEFONTW choose{sizeof(choose)};
        choose.hwndOwner = window;
        choose.lpLogFont = &lf;
        choose.Flags = CF_INITTOLOGFONTSTRUCT | CF_SCREENFONTS | CF_LIMITSIZE;
        choose.nSizeMin = 8;
        choose.nSizeMax = 30;
        if (ChooseFontW(&choose)) {
            font_name = lf.lfFaceName;
            font_size = std::clamp(choose.iPointSize / 10, 8, 30);
            style_editor();
        }
        break;
    }
    case Palette:
        show_palette();
        break;
    case Recent: {
        auto menu = CreatePopupMenu();
        for (size_t n = 0; n < recent.size(); ++n)
            add_menu(menu, 1000 + n, recent[n].filename().wstring());
        if (recent.empty())
            add_menu(menu, 0, L"No recent files", MF_GRAYED);
        POINT p{};
        GetCursorPos(&p);
        int chosen = TrackPopupMenu(menu, TPM_RETURNCMD, p.x, p.y, 0, window, nullptr);
        DestroyMenu(menu);
        if (chosen >= 1000 && size_t(chosen - 1000) < recent.size())
            open_file(recent[size_t(chosen - 1000)]);
        break;
    }
    case ClearRecent:
        recent.clear();
        save_settings();
        status = L"Recent file history cleared";
        break;
    case History:
        history = !history;
        save_settings();
        status = history ? L"Recent history enabled" : L"Recent history disabled";
        break;
    case RestoreSession:
        restore = !restore;
        save_settings();
        status = restore ? L"Clean tabs will reopen next launch" : L"Clean tab restore disabled";
        break;
    case Recovery:
        recovery = !recovery;
        save_settings();
        if (recovery)
            schedule_recovery();
        else
            KillTimer(window, 1);
        status = recovery ? L"Unsaved-buffer recovery enabled"
                          : L"Recovery disabled · existing snapshots retained";
        break;
    case ClearRecovery:
        if (MessageBoxW(
                window,
                L"Clear stored recovery text? This does not delete original documents or current "
                L"buffers. New snapshots may be written for dirty tabs if recovery stays enabled.",
                L"Clear recovery", MB_YESNO | MB_ICONWARNING) == IDYES) {
            save_session(data_dir / L"session.dat", {});
            preserve_recovery = false;
            status = L"Stored recovery cleared";
        }
        break;
    case Enc8:
        change_encoding(Encoding::utf8);
        break;
    case Enc8Bom:
        change_encoding(Encoding::utf8bom);
        break;
    case Enc16LE:
        change_encoding(Encoding::utf16le);
        break;
    case Enc16BE:
        change_encoding(Encoding::utf16be);
        break;
    case EolLF:
        change_eol(Eol::lf);
        break;
    case EolCRLF:
        change_eol(Eol::crlf);
        break;
    case EolCR:
        change_eol(Eol::cr);
        break;
    case NextTab:
        select((active + 1) % int(docs.size()));
        break;
    case PrevTab:
        select((active + int(docs.size()) - 1) % int(docs.size()));
        break;
    case MoveLeft:
    case MoveRight: {
        int n = active + (id == MoveLeft ? -1 : 1);
        if (n >= 0 && n < int(docs.size())) {
            std::swap(docs[size_t(active)], docs[size_t(n)]);
            active = -1;
            select(n);
            update_tabs();
        }
        break;
    }
    case Help: {
        wchar_t path[32768]{};
        GetModuleFileNameW(nullptr, path, 32768);
        auto help = fs::path(path).parent_path() / L"HELP.md";
        if (fs::exists(help)) {
            auto loaded = load(help);
            loaded.read_only = true;
            add_document(std::move(loaded));
        } else
            MessageBoxW(window,
                        L"Ctrl+N New · Ctrl+O Open · Ctrl+S Save\nCtrl+F Find · Ctrl+H Replace · "
                        L"Ctrl+G Go to\nCtrl+Shift+P Commands · F11 Focus\n\nFull offline guide: "
                        L"HELP.md in the distribution.",
                        L"Indic NotePad Help", MB_OK | MB_ICONINFORMATION);
        break;
    }
    case About:
        MessageBoxW(window,
                    L"Indic NotePad 0.1.0-beta.1\nA native text editor from IndicStudio\n\nMIT "
                    L"open source · Windows x64 · Offline\nScintilla 5.6.7 · Lexilla 5.5.4 · PCRE2 "
                    L"10.49\n\nhttps://indicstudio.com/tools/indic-notepad/\nhttps://github.com/"
                    L"ranjanlive/indic-notepad\nsupport@indicstudio.com\n\nUnsigned beta. See "
                    L"HELP.md and bundled licenses.\nNo accounts, telemetry or background service.",
                    L"About Indic NotePad", MB_OK | MB_ICONINFORMATION);
        break;
    }
    invalidate();
}
void draw_item(DRAWITEMSTRUCT&);
LRESULT CALLBACK tabs_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_ERASEBKGND)
        return 1;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps{};
        auto dc = BeginPaint(w, &ps);
        RECT r{};
        GetClientRect(w, &r);
        fill(dc, r, app.c.panel);
        for (int n = 0; n < int(app.docs.size()); ++n) {
            RECT item{};
            TabCtrl_GetItemRect(w, n, &item);
            if (item.right < 0 || item.left > r.right)
                continue;
            DRAWITEMSTRUCT draw{};
            draw.CtlType = ODT_TAB;
            draw.itemID = UINT(n);
            draw.hDC = dc;
            draw.rcItem = item;
            draw_item(draw);
        }
        EndPaint(w, &ps);
        return 0;
    }
    if (msg == WM_LBUTTONDOWN) {
        TCHITTESTINFO hit{{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, 0};
        int n = TabCtrl_HitTest(w, &hit);
        if (n >= 0) {
            RECT r{};
            TabCtrl_GetItemRect(w, n, &r);
            if (GET_X_LPARAM(lp) > r.right - app.s(26)) {
                try {
                    app.close_doc(n);
                } catch (const std::exception& e) {
                    app.error(e.what());
                }
                return 0;
            }
            app.drag_tab = n;
            app.drag_start = hit.pt;
        }
    }
    if (msg == WM_LBUTTONUP && app.drag_tab >= 0) {
        TCHITTESTINFO hit{{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, 0};
        int n = TabCtrl_HitTest(w, &hit);
        int from = app.drag_tab;
        app.drag_tab = -1;
        if (!app.busy && n >= 0 && n != from && from < int(app.docs.size()) &&
            std::abs(hit.pt.x - app.drag_start.x) > app.s(12)) {
            auto d = std::move(app.docs[size_t(from)]);
            app.docs.erase(app.docs.begin() + from);
            app.docs.insert(app.docs.begin() + n, std::move(d));
            app.active = -1;
            app.select(n);
            app.update_tabs();
            return 0;
        }
    }
    if (msg == WM_MBUTTONUP) {
        TCHITTESTINFO hit{{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, 0};
        int n = TabCtrl_HitTest(w, &hit);
        if (n >= 0)
            app.close_doc(n);
        return 0;
    }
    return DefSubclassProc(w, msg, wp, lp);
}
LRESULT CALLBACK search_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        try {
            app.command(LOWORD(wp));
        } catch (const std::exception& e) {
            app.error(e.what());
        }
        return 0;
    case WM_ERASEBKGND: {
        RECT r{};
        GetClientRect(w, &r);
        FillRect(reinterpret_cast<HDC>(wp), &r, app.panel_brush);
        return 1;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetTextColor(reinterpret_cast<HDC>(wp), app.c.text);
        SetBkColor(reinterpret_cast<HDC>(wp), app.c.panel);
        return reinterpret_cast<LRESULT>(app.panel_brush);
    case WM_DRAWITEM:
        return SendMessageW(app.window, msg, wp, lp);
    }
    return DefWindowProcW(w, msg, wp, lp);
}
bool shortcut(MSG& msg) {
    if (msg.message != WM_KEYDOWN)
        return false;
    bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0,
         alt = GetKeyState(VK_MENU) < 0;
    int id = 0;
    bool edit_focus = GetFocus() == app.editor;
    if (ctrl) {
        switch (msg.wParam) {
        case 'N':
            id = New;
            break;
        case 'O':
            id = Open;
            break;
        case 'S':
            id = shift ? (alt ? SaveAll : SaveAs) : Save;
            break;
        case 'W':
            id = Close;
            break;
        case 'F':
            id = Find;
            break;
        case 'H':
            id = Replace;
            break;
        case 'G':
            id = GoTo;
            break;
        case 'P':
            if (shift)
                id = Palette;
            break;
        case VK_TAB:
            id = shift ? PrevTab : NextTab;
            break;
        case VK_OEM_PLUS:
        case VK_ADD:
            id = ZoomIn;
            break;
        case VK_OEM_MINUS:
        case VK_SUBTRACT:
            id = ZoomOut;
            break;
        case '0':
            id = ZoomReset;
            break;
        case 'Z':
            if (edit_focus)
                id = Undo;
            break;
        case 'Y':
            if (edit_focus)
                id = Redo;
            break;
        }
    } else if (msg.wParam == VK_F3)
        id = shift ? FindPrev : FindNext;
    else if (msg.wParam == VK_F11)
        id = Focus;
    else if (msg.wParam == VK_ESCAPE) {
        if (app.busy) {
            app.cancel = true;
            return true;
        }
        if (app.focus)
            id = Focus;
        else if (IsWindowVisible(app.search_panel))
            id = CloseSearch;
    } else if (msg.wParam == VK_RETURN &&
               (msg.hwnd == app.find_edit || msg.hwnd == app.replace_edit))
        id = msg.hwnd == app.replace_edit ? ReplaceButton : FindNext;
    if (id) {
        try {
            app.command(id);
        } catch (const std::exception& e) {
            app.error(e.what());
        }
        return true;
    }
    return false;
}
void draw_item(DRAWITEMSTRUCT& item) {
    auto& a = app;
    auto dc = item.hDC;
    auto r = item.rcItem;
    if (item.CtlType == ODT_MENU) {
        auto text = reinterpret_cast<const std::wstring*>(item.itemData);
        if (!text)
            return;
        bool selected = item.itemState & ODS_SELECTED;
        fill(dc, r, selected ? a.c.selected : a.c.bg);
        std::wstring name = *text;
        auto tab = name.find('\t');
        auto left = name.substr(0, tab);
        r.left += a.s(18);
        r.right -= a.s(18);
        label(dc, left, r, a.ui_font, (item.itemState & ODS_DISABLED) ? a.c.muted : a.c.text);
        if (tab != std::wstring::npos)
            label(dc, name.substr(tab + 1), r, a.small_font, a.c.muted,
                  DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        if (item.itemState & ODS_CHECKED)
            label(
                dc, L"✓",
                {item.rcItem.left, item.rcItem.top, item.rcItem.left + a.s(18), item.rcItem.bottom},
                a.small_font, a.c.accent);
        return;
    }
    if (item.CtlType == ODT_TAB) {
        int n = int(item.itemID);
        if (n < 0 || n >= int(a.docs.size()))
            return;
        bool selected = n == a.active;
        fill(dc, r, selected ? a.c.editor : a.c.panel);
        if (selected)
            fill(dc, {r.left, r.top, r.right, r.top + a.s(2)}, a.c.accent);
        auto& d = *a.docs[size_t(n)];
        auto title = d.title + (a.dirty(d) ? L"  ●" : L"") + (d.read_only ? L"  R" : L"");
        r.left += a.s(14);
        r.right -= a.s(25);
        label(dc, title, r, selected ? a.bold_font : a.ui_font, selected ? a.c.text : a.c.muted);
        auto end = item.rcItem;
        label(dc, L"×", {end.right - a.s(23), end.top, end.right - a.s(4), end.bottom}, a.ui_font,
              a.c.muted, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        line(dc, end.right - 1, end.top, end.right - 1, end.bottom, a.c.line);
        return;
    }
    if (item.CtlType == ODT_BUTTON) {
        bool pressed = item.itemState & ODS_SELECTED;
        fill(dc, r, pressed ? a.c.selected : a.c.panel);
        auto text = get_text(item.hwndItem);
        label(dc, text, r, a.ui_font, item.itemState & ODS_DISABLED ? a.c.muted : a.c.text,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (item.itemState & ODS_FOCUS) {
            InflateRect(&r, -2, -2);
            DrawFocusRect(dc, &r);
        }
        return;
    }
}
LRESULT CALLBACK window_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    auto& a = app;
    try {
        switch (msg) {
        case WM_CREATE: {
            a.window = w;
            a.dpi = int(GetDpiForWindow(w));
            a.icon = LoadIconW(a.instance, MAKEINTRESOURCEW(1));
            a.editor =
                CreateWindowExW(0, L"Scintilla", L"Document editor",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN, 0, 0, 100,
                                100, w, reinterpret_cast<HMENU>(500), a.instance, nullptr);
            if (!a.editor)
                throw Error("Cannot create the native editor.");
            a.sci(SCI_SETTECHNOLOGY, SC_TECHNOLOGY_DIRECTWRITEDC);
            a.sci(SCI_SETCODEPAGE, SC_CP_UTF8);
            a.sci(SCI_SETMODEVENTMASK, SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT);
            a.sci(SCI_SETACCESSIBILITY, SC_ACCESSIBILITY_ENABLED);
            a.sci(SCI_SETCOMMANDEVENTS, 0);
            a.tabs = CreateWindowExW(0, WC_TABCONTROLW, L"Open documents",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | TCS_OWNERDRAWFIXED |
                                         TCS_SINGLELINE | TCS_FOCUSNEVER | TCS_FIXEDWIDTH,
                                     0, 0, 100, 100, w, reinterpret_cast<HMENU>(Tabs), a.instance,
                                     nullptr);
            TabCtrl_SetItemSize(a.tabs, a.s(160), a.s(34));
            SetWindowSubclass(a.tabs, tabs_proc, 1, 0);
            a.tree = CreateWindowExW(
                0, WC_TREEVIEWW, L"Workspace files",
                WS_CHILD | WS_TABSTOP | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS, 0, 0,
                100, 100, w, reinterpret_cast<HMENU>(Tree), a.instance, nullptr);
            a.search_panel = CreateWindowExW(0, L"IndicNotePadSearch", L"Find and replace",
                                             WS_CHILD | WS_CLIPCHILDREN, 0, 0, 100, 100, w, nullptr,
                                             a.instance, nullptr);
            auto edit = [&](int id, const wchar_t* name) {
                auto h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0,
                                         100, 28, a.search_panel,
                                         reinterpret_cast<HMENU>(INT_PTR(id)), a.instance, nullptr);
                SetWindowTextW(h, L"");
                SendMessageW(h, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(name));
                SendMessageW(h, EM_SETLIMITTEXT, 4096, 0);
                return h;
            };
            a.find_edit = edit(FindEdit, L"Find in this document…");
            a.replace_edit = edit(ReplaceEdit, L"Literal replacement text…");
            for (auto [id, name] :
                 std::vector<std::pair<int, const wchar_t*>>{{PrevButton, L"Previous"},
                                                             {NextButton, L"Next"},
                                                             {ReplaceButton, L"Replace"},
                                                             {ReplaceAllButton, L"Replace all"},
                                                             {CloseSearch, L"×"}})
                a.search_buttons.push_back(button(a.search_panel, id, name));
            auto check = [&](int id, const wchar_t* text) {
                return CreateWindowW(L"BUTTON", text,
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0,
                                     100, 20, a.search_panel, reinterpret_cast<HMENU>(INT_PTR(id)),
                                     a.instance, nullptr);
            };
            a.case_check = check(CaseCheck, L"Match case");
            a.word_check = check(WordCheck, L"Whole word");
            a.regex_check = check(RegexCheck, L"Regex");
            for (auto [id, name] :
                 std::vector<std::pair<int, const wchar_t*>>{{New, L"＋ New"},
                                                             {Open, L"Open"},
                                                             {Save, L"Save"},
                                                             {Undo, L"Undo"},
                                                             {Redo, L"Redo"},
                                                             {Find, L"Find"},
                                                             {Wrap, L"Wrap"},
                                                             {Palette, L"Commands"}})
                a.toolbar.push_back(button(w, id, name));
            a.menubar = a.make_menu();
            SetMenu(w, a.menubar);
            a.apply_theme();
            DragAcceptFiles(w, TRUE);
            return 0;
        }
        case WM_SIZE:
            a.layout();
            return 0;
        case WM_LBUTTONDOWN: {
            RECT r{};
            GetClientRect(w, &r);
            int x = GET_X_LPARAM(lp);
            if (IsWindowVisible(a.tree) && std::abs(x - a.s(a.sidebar_width)) < a.s(5))
                a.resize_pane = 1;
            else if (IsWindowVisible(a.reader) &&
                     std::abs(x - (r.right - a.s(a.preview_width))) < a.s(5))
                a.resize_pane = 2;
            if (a.resize_pane)
                SetCapture(w);
            return 0;
        }
        case WM_MOUSEMOVE:
            if (a.resize_pane) {
                RECT r{};
                GetClientRect(w, &r);
                int x = GET_X_LPARAM(lp);
                if (a.resize_pane == 1)
                    a.sidebar_width = std::clamp(MulDiv(x, 96, a.dpi), 160, 360);
                else
                    a.preview_width = std::clamp(MulDiv(r.right - x, 96, a.dpi), 240, 480);
                a.layout();
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
            }
            return 0;
        case WM_LBUTTONUP:
            if (a.resize_pane) {
                a.resize_pane = 0;
                ReleaseCapture();
            }
            return 0;
        case HISTORY_LIMIT: {
            auto d = a.by_id(uint64_t(wp));
            if (!d || d->undo_bytes == 0)
                return 0;
            auto old = a.sci(SCI_GETDOCPOINTER), cursor = a.sci(SCI_GETCURRENTPOS),
                 anchor = a.sci(SCI_GETANCHOR), scroll = a.sci(SCI_GETFIRSTVISIBLELINE);
            bool was_dirty = a.dirty(*d);
            a.loading = true;
            a.sci(SCI_SETDOCPOINTER, 0, d->pointer);
            a.sci(SCI_EMPTYUNDOBUFFER);
            a.sci(SCI_SETDOCPOINTER, 0, old);
            a.sci(SCI_SETSEL, anchor, cursor);
            a.sci(SCI_SETFIRSTVISIBLELINE, scroll);
            a.loading = false;
            d->undo_bytes = 0;
            d->metadata_dirty |= was_dirty;
            a.status = L"Undo history reset at its memory budget · text preserved";
            a.update_tabs();
            a.invalidate();
            return 0;
        }
        case INPUT_LIMIT: {
            auto d = a.by_id(uint64_t(wp));
            if (!d || d != a.current())
                return 0;
            uint64_t total = 0;
            for (auto& item : a.docs)
                total += item->bytes;
            if (d->bytes <= edit_limit && total <= 128ull * 1024 * 1024)
                return 0;
            if (a.sci(SCI_CANUNDO))
                a.sci(SCI_UNDO);
            else {
                d->read_only = true;
                a.sci(SCI_SETREADONLY, 1);
            }
            a.error(
                "The edit exceeded the 32 MiB document or 128 MiB total text budget. The last edit "
                "was undone where possible. Save and close other tabs before adding more text.");
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto info = reinterpret_cast<MINMAXINFO*>(lp);
            info->ptMinTrackSize = {a.s(760), a.s(460)};
            return 0;
        }
        case WM_DPICHANGED: {
            a.dpi = HIWORD(wp);
            auto r = reinterpret_cast<RECT*>(lp);
            SetWindowPos(w, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            a.apply_theme();
            TabCtrl_SetItemSize(a.tabs, a.s(160), a.s(34));
            return 0;
        }
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            a.apply_theme();
            a.refresh_preview();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            a.paint();
            return 0;
        case WM_DRAWITEM:
            draw_item(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));
            return TRUE;
        case WM_MEASUREITEM: {
            auto item = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
            if (item->CtlType == ODT_MENU) {
                auto text = reinterpret_cast<std::wstring*>(item->itemData);
                auto dc = GetDC(w);
                SelectObject(dc, a.ui_font);
                SIZE size{};
                GetTextExtentPoint32W(dc, text->c_str(), int(text->size()), &size);
                ReleaseDC(w, dc);
                item->itemWidth =
                    UINT(size.cx + a.s(text->find('\t') != std::wstring::npos ? 58 : 30));
                item->itemHeight = UINT(a.s(29));
                return TRUE;
            }
            if (item->CtlType == ODT_TAB) {
                item->itemWidth = UINT(a.s(180));
                item->itemHeight = UINT(a.s(34));
                return TRUE;
            }
            break;
        }
        case WM_INITMENUPOPUP: {
            auto menu = reinterpret_cast<HMENU>(wp);
            for (auto [id, on] : std::vector<std::pair<int, bool>>{{Wrap, a.wrap},
                                                                   {Lines, a.numbers},
                                                                   {Whitespace, a.whitespace},
                                                                   {Sidebar, a.sidebar},
                                                                   {Preview, a.preview},
                                                                   {Focus, a.focus},
                                                                   {Dark, a.theme == 0},
                                                                   {Light, a.theme == 1},
                                                                   {System, a.theme == 2},
                                                                   {Recovery, a.recovery},
                                                                   {History, a.history},
                                                                   {RestoreSession, a.restore}})
                CheckMenuItem(menu, UINT(id), MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
            if (auto d = a.current()) {
                CheckMenuRadioItem(menu, Enc8, Enc16BE, Enc8 + int(d->encoding), MF_BYCOMMAND);
                for (int id : {Save, Reload, Cut, Paste})
                    EnableMenuItem(
                        menu, UINT(id),
                        MF_BYCOMMAND |
                            ((a.busy || (d->read_only && id != Save)) ? MF_GRAYED : MF_ENABLED));
            }
            return 0;
        }
        case WM_COMMAND:
            a.command(LOWORD(wp));
            return 0;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
            SetTextColor(reinterpret_cast<HDC>(wp), a.c.text);
            SetBkColor(reinterpret_cast<HDC>(wp), a.c.editor);
            return reinterpret_cast<LRESULT>(a.editor_brush);
        case WM_NOTIFY: {
            auto hdr = reinterpret_cast<NMHDR*>(lp);
            if (hdr->hwndFrom == a.tabs && hdr->code == TCN_SELCHANGE) {
                if (!a.busy)
                    a.select(TabCtrl_GetCurSel(a.tabs));
                else
                    TabCtrl_SetCurSel(a.tabs, a.active);
                return 0;
            }
            if (hdr->hwndFrom == a.tree) {
                if (hdr->code == TVN_ITEMEXPANDINGW) {
                    auto event = reinterpret_cast<NMTREEVIEWW*>(lp);
                    if (event->action == TVE_EXPAND &&
                        !(event->itemNew.state & TVIS_EXPANDEDONCE)) {
                        for (auto& [item, path] : a.tree_paths)
                            if (item == event->itemNew.hItem) {
                                auto copied = path;
                                a.populate_tree(item, copied);
                                break;
                            }
                    }
                } else if (hdr->code == NM_DBLCLK ||
                           (hdr->code == TVN_KEYDOWN &&
                            reinterpret_cast<NMTVKEYDOWN*>(lp)->wVKey == VK_RETURN)) {
                    auto selected = TreeView_GetSelection(a.tree);
                    for (auto& [item, path] : a.tree_paths)
                        if (item == selected && !fs::is_directory(path)) {
                            a.open_file(path);
                            break;
                        }
                }
                return 0;
            }
            if (hdr->hwndFrom == a.editor) {
                auto event = reinterpret_cast<SCNotification*>(lp);
                if (event->nmhdr.code == SCN_MODIFIED && !a.loading && a.current()) {
                    auto d = a.current();
                    if (event->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) {
                        if (event->text && event->length > 0) {
                            auto count =
                                characters(std::string_view(event->text, size_t(event->length)));
                            if (event->modificationType & SC_MOD_INSERTTEXT) {
                                d->chars += count;
                                d->bytes += uint64_t(event->length);
                            } else {
                                d->chars = d->chars >= count ? d->chars - count : 0;
                                d->bytes = d->bytes >= uint64_t(event->length)
                                               ? d->bytes - uint64_t(event->length)
                                               : 0;
                            }
                            if (!(event->modificationType &
                                  (SC_PERFORMED_UNDO | SC_PERFORMED_REDO)))
                                d->undo_bytes += uint64_t(event->length);
                            uint64_t total = 0, undo_total = 0;
                            for (auto& item : a.docs) {
                                total += item->bytes;
                                undo_total += item->undo_bytes;
                            }
                            if (d->bytes > edit_limit || total > 128ull * 1024 * 1024)
                                PostMessageW(w, INPUT_LIMIT, WPARAM(d->id), 0);
                            if (d->undo_bytes > 16ull * 1024 * 1024 ||
                                undo_total > 64ull * 1024 * 1024)
                                PostMessageW(w, HISTORY_LIMIT, WPARAM(d->id), 0);
                            if (event->linesAdded) {
                                auto part = std::string_view(event->text, size_t(event->length));
                                auto incoming = detect_eol(part);
                                if (d->eol == Eol::none)
                                    d->eol = incoming;
                                else if (incoming != Eol::none && incoming != d->eol)
                                    d->eol = Eol::mixed;
                            }
                        }
                        a.changed();
                    }
                } else if (event->nmhdr.code == SCN_SAVEPOINTLEFT ||
                           event->nmhdr.code == SCN_SAVEPOINTREACHED) {
                    if (!a.loading && a.current()) {
                        a.current()->text_dirty = event->nmhdr.code == SCN_SAVEPOINTLEFT;
                        a.update_tabs();
                    }
                } else if (event->nmhdr.code == SCN_UPDATEUI) {
                    ++a.update_count;
                    RECT r{};
                    GetClientRect(w, &r);
                    r.top = r.bottom - a.s(32);
                    InvalidateRect(w, &r, FALSE);
                } else if (event->nmhdr.code == SCN_CHARADDED && event->ch == '\n' && !a.loading) {
                    auto pos = a.sci(SCI_GETCURRENTPOS);
                    auto row = a.sci(SCI_LINEFROMPOSITION, pos);
                    if (row > 0) {
                        auto indent = a.sci(SCI_GETLINEINDENTATION, row - 1);
                        a.sci(SCI_SETLINEINDENTATION, row, indent);
                        a.sci(SCI_GOTOPOS, a.sci(SCI_GETLINEINDENTPOSITION, row));
                    }
                }
            }
            return 0;
        }
        case WM_DROPFILES: {
            auto drop = reinterpret_cast<HDROP>(wp);
            UINT count = DragQueryFileW(drop, 0xffffffff, nullptr, 0);
            wchar_t path[32768]{};
            for (UINT n = 0; n < std::min<UINT>(count, 50); ++n)
                if (DragQueryFileW(drop, n, path, 32768))
                    a.open_file(path);
            DragFinish(drop);
            return 0;
        }
        case WM_TIMER:
            ++a.app_timer_count;
            if (wp == 1) {
                KillTimer(w, 1);
                a.checkpoint();
            } else if (wp == 2) {
                KillTimer(w, 2);
                a.refresh_preview();
            } else if (wp == 3) {
                KillTimer(w, 3);
                SendMessageW(w, WM_CLOSE, 0, 0);
            }
            return 0;
        case JOB_DONE: {
            std::unique_ptr<std::function<void()>> job(
                reinterpret_cast<std::function<void()>*>(lp));
            if (a.worker.joinable())
                a.worker.join();
            a.busy = false;
            if (auto d = a.current())
                a.sci(SCI_SETREADONLY, d->read_only);
            if (job && *job)
                (*job)();
            if (a.deferred_recovery) {
                a.deferred_recovery = false;
                a.schedule_recovery();
            }
            while (!a.busy && !a.pending_files.empty()) {
                auto path = a.pending_files.front();
                a.pending_files.pop_front();
                a.open_file(path);
            }
            a.invalidate();
            return 0;
        }
        case TEST_RUN:
            a.run_tests();
            return 0;
        case WM_QUERYENDSESSION:
            if (a.recovery)
                a.checkpoint();
            return a.close_window() ? TRUE : FALSE;
        case WM_CLOSE:
            if (a.close_window())
                DestroyWindow(w);
            return 0;
        case WM_DESTROY:
            if (a.worker.joinable()) {
                a.cancel = true;
                a.worker.join();
            }
            for (auto& d : a.docs)
                a.sci(SCI_RELEASEDOCUMENT, 0, d->pointer);
            a.docs.clear();
            PostQuitMessage(0);
            return 0;
        }
    } catch (const std::exception& e) {
        a.error(e.what());
    }
    return DefWindowProcW(w, msg, wp, lp);
}
void App::run_tests() {
#ifdef NOTEPAD_TEST_BUILD
    std::vector<std::string> passed;
    int exit_code = 0;
    try {
        auto check = [&](bool condition, const char* name) {
            if (!condition)
                throw Error(name);
            passed.emplace_back(name);
        };
        check(palette_commands.size() > 40, "command-palette-populated");
        new_document();
        sci(SCI_ADDTEXT, 11, reinterpret_cast<sptr_t>("Hello world"));
        check(dirty(*current()) && text() == "Hello world", "native-edit-and-dirty");
        sci(SCI_UNDO);
        check(text().empty(), "native-undo");
        sci(SCI_REDO);
        check(text() == "Hello world", "native-redo");
        current()->path = test_dir / L"saved.txt";
        current()->stamp = {};
        check(save_doc(), "atomic-save-from-editor");
        check(read_bytes(test_dir / L"saved.txt", 100) == "Hello world", "disk-matches-editor");
        auto first = active;
        sci(SCI_SETSEL, 1, 5);
        new_document();
        sci(SCI_ADDTEXT, 5, reinterpret_cast<sptr_t>("other"));
        select(first);
        check(text() == "Hello world" && sci(SCI_GETSELECTIONSTART) == 1,
              "tab-document-and-selection");
        sci(SCI_SETTARGETSTART, 6);
        sci(SCI_SETTARGETEND, 11);
        sci(SCI_REPLACETARGET, 5, reinterpret_cast<sptr_t>("Indic"));
        check(text() == "Hello Indic", "native-replace");
        sci(SCI_UNDO);
        check(text() == "Hello world", "replace-undo");
        show_search(false);
        SetWindowTextW(find_edit, L"world");
        sci(SCI_SETSEL, 0, 0);
        find();
        while (busy) {
            MSG message{};
            if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            } else
                Sleep(2);
        }
        check(sci(SCI_GETSELECTIONSTART) == 6 && sci(SCI_GETSELECTIONEND) == 11,
              "native-find-panel-to-worker-to-selection");
        SetWindowTextW(replace_edit, L"Indic");
        find(false, 1);
        while (busy) {
            MSG message{};
            if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            } else
                Sleep(2);
        }
        check(text() == "Hello Indic", "native-replace-panel-to-buffer");
        sci(SCI_UNDO);
        command(CloseSearch);
        command(Dark);
        check(dark, "dark-theme");
        command(Light);
        check(!dark, "light-theme");
        command(Wrap);
        check(sci(SCI_GETWRAPMODE) == SC_WRAP_NONE, "word-wrap-toggle");
        command(Wrap);
        command(Lines);
        check(sci(SCI_GETMARGINWIDTHN, 0) == 0, "line-numbers-toggle");
        command(Lines);
        Loaded md;
        md.path = test_dir / L"plan.md";
        md.text = "# Indic NotePad\n\nYour words, in focus.\n\n- Native Windows editor\n- Offline "
                  "files\n";
        add_document(std::move(md));
        preview = true;
        layout();
        refresh_preview();
        check(get_text(reader).find(L"Indic NotePad") != std::wstring::npos,
              "native-markdown-reading-pane");
        sci(SCI_ADDTEXT, 8, reinterpret_cast<sptr_t>("Recovery"));
        checkpoint();
        while (busy) {
            MSG message{};
            if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            } else
                Sleep(2);
        }
        auto recovered = read_session(data_dir / L"session.dat");
        check(std::any_of(
                  recovered.begin(), recovered.end(),
                  [](auto& d) { return d.dirty && d.text.find("Recovery") != std::string::npos; }),
              "dirty-buffer-recovery-snapshot");
        while (docs.size() < 50)
            new_document();
        check(docs.size() == 50, "fifty-small-tabs");
        bool refused = false;
        try {
            new_document();
        } catch (const Error&) {
            refused = true;
        }
        check(refused, "tab-budget-enforced");
    } catch (const std::exception& e) {
        exit_code = 1;
        passed.push_back(std::string("FAILED: ") + e.what());
    }
    std::string json =
        "{\n  \"passed\": " + std::string(exit_code ? "false" : "true") + ",\n  \"checks\": [";
    for (size_t n = 0; n < passed.size(); ++n) {
        if (n)
            json += ",";
        json += "\n    \"" + passed[n] + "\"";
    }
    json += "\n  ],\n  \"method\": \"Visible native test executable; real Scintilla controls and "
            "actual disk IO, no OS input simulation.\"\n}\n";
    write_atomic(test_dir / L"ui-smoke.json", json);
    quitting = true;
    DestroyWindow(window);
    PostQuitMessage(exit_code);
#endif
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    app.instance = instance;
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int result = 0;
    try {
        int argc{};
        auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::vector<fs::path> files;
        bool portable = false, after = false;
        for (int n = 1; n < argc; ++n) {
            std::wstring arg = argv[n];
            if (!after && arg == L"--") {
                after = true;
                continue;
            }
            if (!after && arg == L"--portable") {
                portable = true;
                continue;
            }
            if (!after && arg == L"--readonly") {
                app.read_only_arg = true;
                continue;
            }
            if (!after && arg == L"--benchmark-ms") {
                if (n + 1 >= argc)
                    throw Error("--benchmark-ms requires a duration.");
                std::wstring duration = argv[++n];
                size_t used{};
                auto ms = std::stoul(duration, &used);
                if (used != duration.size() || ms < 1000 || ms > 30000)
                    throw Error("Benchmark duration must be 1000–30000 milliseconds.");
                app.benchmark_ms = UINT(ms);
                continue;
            }
            if (!after && arg == L"--data-dir") {
                if (n + 1 >= argc)
                    throw Error("--data-dir requires a folder.");
                app.data_dir = argv[++n];
                continue;
            }
#ifdef NOTEPAD_TEST_BUILD
            if (arg == L"--smoke-test") {
                if (n + 1 >= argc)
                    throw Error("--smoke-test requires a fresh test directory.");
                app.test_dir = fs::absolute(argv[++n]);
                if (fs::exists(app.test_dir))
                    throw Error("Test directory already exists.");
                fs::create_directories(app.test_dir);
                app.data_dir = app.test_dir / L"profile";
                continue;
            }
#endif
            if (!after && (arg == L"--help" || arg == L"--version")) {
                MessageBoxW(nullptr,
                            arg == L"--version"
                                ? L"Indic NotePad 0.1.0-beta.1"
                                : L"Indic NotePad\n\nindic-notepad.exe [--portable | --data-dir "
                                  L"folder] [--readonly] [--] [files...]\n\nNative offline Windows "
                                  L"editor. No browser runtime.\nSee HELP.md for keyboard "
                                  L"shortcuts, file limits and recovery.",
                            L"Indic NotePad", MB_OK | MB_ICONINFORMATION);
                LocalFree(argv);
                if (SUCCEEDED(hr))
                    CoUninitialize();
                return 0;
            }
            if (!after && arg.starts_with(L"--"))
                throw Error("Unknown command-line flag.");
            files.emplace_back(arg);
        }
        LocalFree(argv);
        if (app.data_dir.empty() && !portable) {
            wchar_t path[32768]{};
            GetModuleFileNameW(nullptr, path, 32768);
            portable = fs::exists(fs::path(path).parent_path() / L"portable.mode");
        }
        if (portable && !app.data_dir.empty())
            throw Error("Use --portable or --data-dir, not both.");
        if (app.benchmark_ms &&
            (app.data_dir.empty() || !files.empty() || fs::exists(app.data_dir)))
            throw Error(
                "Benchmark mode requires an explicitly selected NEW --data-dir and no files.");
        if (app.data_dir.empty()) {
            if (portable) {
                wchar_t path[32768]{};
                GetModuleFileNameW(nullptr, path, 32768);
                app.data_dir = fs::path(path).parent_path() / L"data";
            } else {
                PWSTR local{};
                if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)))
                    throw Error("Cannot locate local settings folder.");
                app.data_dir = fs::path(local) / L"IndicStudio" / L"IndicNotePad";
                CoTaskMemFree(local);
            }
        }
        fs::create_directories(app.data_dir);
        app.profile_lock = std::make_unique<Handle>(
            CreateFileW((app.data_dir / L"profile.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!*app.profile_lock)
            throw Error("This profile is already in use. Close the other Indic NotePad window or "
                        "select a different --data-dir.");
        app.load_settings();
        if (app.benchmark_ms) {
            app.recovery = false;
            app.restore = false;
            app.history = false;
        }
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TAB_CLASSES | ICC_TREEVIEW_CLASSES |
                                                            ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&controls);
        if (!Scintilla_RegisterClasses(instance))
            throw Error("Scintilla registration failed.");
        WNDCLASSEXW wc{sizeof(wc)};
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
        wc.hIconSm = wc.hIcon;
        wc.lpfnWndProc = window_proc;
        wc.lpszClassName = L"IndicNotePadWindow";
        wc.style = CS_DBLCLKS;
        RegisterClassExW(&wc);
        wc.lpfnWndProc = search_proc;
        wc.lpszClassName = L"IndicNotePadSearch";
        RegisterClassExW(&wc);
        wc.lpfnWndProc = modal_proc;
        wc.lpszClassName = L"IndicNotePadModal";
        RegisterClassExW(&wc);
        auto window = CreateWindowExW(
            0, L"IndicNotePadWindow", L"Indic NotePad", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT, 1240, 820, nullptr, nullptr, instance, nullptr);
        if (!window)
            throw Error("Cannot create Indic NotePad window.");
        ShowWindow(window, show);
        UpdateWindow(window);
#ifdef NOTEPAD_TEST_BUILD
        if (!app.test_dir.empty())
            PostMessageW(window, TEST_RUN, 0, 0);
        else
#endif
        {
            app.restore_session();
            if (app.docs.empty())
                app.new_document();
            for (auto& file : files) {
                try {
                    app.open_file(file);
                } catch (const std::exception& e) {
                    app.error(e.what());
                }
            }
        }
        if (app.benchmark_ms) {
            std::string fixture;
            while (fixture.size() < 10 * 1024)
                fixture += "A small note. One useful idea, captured locally.\n";
            fixture.resize(10 * 1024);
            app.loading = true;
            app.sci(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(fixture.c_str()));
            app.sci(SCI_EMPTYUNDOBUFFER);
            app.sci(SCI_SETSAVEPOINT);
            app.current()->text_dirty = false;
            app.current()->bytes = fixture.size();
            app.current()->chars = characters(fixture);
            app.loading = false;
            app.update_tabs();
            SetTimer(window, 3, app.benchmark_ms, nullptr);
        }
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (shortcut(msg))
                continue;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        result = int(msg.wParam);
        Scintilla_ReleaseResources();

    } catch (const std::exception& e) {
        MessageBoxW(nullptr, wide(e.what()).c_str(), L"Indic NotePad could not start",
                    MB_OK | MB_ICONERROR);
        result = 1;
    }
    if (SUCCEEDED(hr))
        CoUninitialize();
    return result;
}
