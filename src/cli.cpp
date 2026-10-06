#include "rse.hpp"
#include <stdexcept>
namespace rse {
Options parse_cli(const std::vector<std::wstring>& args) {
    Options o;
    for (const auto& arg : args) {
        if (arg == L"--json" && !o.json) o.json = true;
        else if (arg == L"--help" && !o.help) o.help = true;
        else if (arg == L"--version" && !o.show_version) o.show_version = true;
        else if (!arg.empty() && arg[0] != L'-' && o.destination.empty()) o.destination = arg;
        else throw std::invalid_argument("Unknown/duplicate option or extra destination. Use --help.");
    }
    if (o.help || o.show_version) {
        if (args.size() != 1) throw std::invalid_argument("--help and --version must be used alone.");
    } else if (o.destination.empty()) throw std::invalid_argument("A numeric destination is required. Use --help.");
    return o;
}
}
