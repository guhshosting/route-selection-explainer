#include "rse.hpp"
#include <iostream>
#include <stdexcept>
namespace {
void write_output(const std::string& text, bool error = false) {
    const HANDLE handle = GetStdHandle(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode)) {
        const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (n <= 0) throw std::runtime_error("Console Unicode conversion failed");
        std::wstring wide(static_cast<size_t>(n), L'\0');
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), n)) throw std::runtime_error("Console Unicode conversion failed");
        size_t offset = 0;
        while (offset < wide.size()) {
            DWORD written = 0;
            const DWORD count = static_cast<DWORD>((wide.size() - offset) > 16384 ? 16384 : wide.size() - offset);
            if (!WriteConsoleW(handle, wide.data() + offset, count, &written, nullptr) || written == 0) throw std::runtime_error("Console write failed");
            offset += written;
        }
    } else {
        auto& stream = error ? std::cerr : std::cout;
        stream << text; stream.flush();
        if (!stream) throw std::runtime_error("Output write failed");
    }
}
}
int wmain(int argc, wchar_t* argv[]) {
    bool json = false;
    for (int i = 1; i < argc; ++i) if (std::wstring(argv[i]) == L"--json") json = true;
    auto fail = [&](int exit_code, const std::string& status, const std::string& message) {
        write_output(json ? rse::format_error_json(status, message, static_cast<unsigned>(exit_code)) : message + "\n", !json);
        return exit_code;
    };
    try {
        rse::Options options;
        try { options = rse::parse_cli(std::vector<std::wstring>(argv + 1, argv + argc)); }
        catch (const std::invalid_argument& ex) { return fail(2, "usage_error", ex.what()); }
        if (options.help) {
            write_output("route-selection-explainer 1.0.0\nUsage: route-selection-explainer [--json] NUMERIC_IP\n       route-selection-explainer --help | --version\nIPv4 and IPv6 numeric destinations only. Scope-dependent IPv6 and %zone syntax are rejected.\nRead-only local Windows route/source selection. No DNS or connectivity probes.\nExit codes: 0 success, 2 usage, 3 invalid destination, 4 no route, 5 API inspection failure, 6 internal failure.\n"); return 0;
        }
        if (options.show_version) { write_output("route-selection-explainer 1.0.0\n"); return 0; }
        rse::Address address;
        try { address = rse::parse_address(options.destination); }
        catch (const std::invalid_argument& ex) { return fail(3, "invalid_destination", ex.what()); }
        const auto result = rse::inspect(address);
        write_output(json ? rse::format_json(result) : rse::format_human(result));
        return result.status == "no_route" ? 4 : result.status == "inspection_error" ? 5 : 0;
    } catch (const std::exception& ex) {
        try { return fail(6, "internal_error", ex.what()); } catch (...) { return 6; }
    } catch (...) { try { return fail(6, "internal_error", "Unexpected internal failure"); } catch (...) { return 6; } }
}
