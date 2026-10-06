#include "rse.hpp"
#include <algorithm>
#include <stdexcept>

namespace rse {
std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!count) throw std::runtime_error("Invalid Unicode text");
    std::string out(static_cast<size_t>(count), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), count, nullptr, nullptr))
        throw std::runtime_error("Unicode conversion failed");
    return out;
}
Address parse_address(const std::wstring& text) {
    if (text.empty() || text.find_first_of(L"%[] \t\r\n") != std::wstring::npos)
        throw std::invalid_argument("Require a numeric IPv4/IPv6 address; scoped (%zone), bracketed addresses and whitespace are unsupported in 1.0.0.");
    Address a;
    if (InetPtonW(AF_INET, text.c_str(), &a.native.Ipv4.sin_addr) == 1) {
        a.native.si_family = AF_INET;
        return a;
    }
    a = {};
    if (InetPtonW(AF_INET6, text.c_str(), &a.native.Ipv6.sin6_addr) != 1)
        throw std::invalid_argument("Invalid numeric IP destination. Hostnames and DNS resolution are unsupported.");
    a.native.si_family = AF_INET6;
    const auto* b = a.native.Ipv6.sin6_addr.u.Byte;
    // Scope cannot safely be inferred for link/site-local unicast or non-global multicast.
    if ((b[0] == 0xfe && (static_cast<unsigned>(b[1]) & 0xc0u) == 0x80u) ||
        (b[0] == 0xfe && (static_cast<unsigned>(b[1]) & 0xc0u) == 0xc0u) ||
        (b[0] == 0xff && (static_cast<unsigned>(b[1]) & 0x0fu) != 0x0eu))
        throw std::invalid_argument("This IPv6 destination requires or depends on an interface scope. Scoped destinations are unsupported in 1.0.0; no interface is guessed.");
    return a;
}
std::string address_text(const Address& a) {
    wchar_t out[INET6_ADDRSTRLEN]{};
    const void* bytes = a.native.si_family == AF_INET ? static_cast<const void*>(&a.native.Ipv4.sin_addr) : static_cast<const void*>(&a.native.Ipv6.sin6_addr);
    if ((a.native.si_family != AF_INET && a.native.si_family != AF_INET6) || !InetNtopW(a.native.si_family, bytes, out, INET6_ADDRSTRLEN))
        throw std::runtime_error("Unexpected address family or address conversion failure");
    std::string text = utf8(out);
    if (a.native.si_family == AF_INET6 && a.native.Ipv6.sin6_scope_id) text += "%" + std::to_string(a.native.Ipv6.sin6_scope_id);
    return text;
}
static const unsigned char* bytes(const Address& a) {
    return a.native.si_family == AF_INET ? reinterpret_cast<const unsigned char*>(&a.native.Ipv4.sin_addr) : a.native.Ipv6.sin6_addr.u.Byte;
}
bool same_address(const Address& a, const Address& b) {
    if (a.native.si_family != b.native.si_family) return false;
    const size_t n = a.native.si_family == AF_INET ? 4 : 16;
    return std::equal(bytes(a), bytes(a) + n, bytes(b)) &&
        (a.native.si_family != AF_INET6 || a.native.Ipv6.sin6_scope_id == b.native.Ipv6.sin6_scope_id);
}
bool matches(const Address& a, const Route& r) {
    if (a.native.si_family != r.prefix.native.si_family || r.length > (a.native.si_family == AF_INET ? 32u : 128u)) return false;
    const auto* x = bytes(a); const auto* y = bytes(r.prefix);
    const unsigned whole = r.length / 8, bits = r.length % 8;
    if (!std::equal(x, x + whole, y)) return false;
    return bits == 0 || ((static_cast<unsigned>(x[whole]) ^ static_cast<unsigned>(y[whole])) & (0xffu << (8 - bits))) == 0;
}
bool on_link(const Route& r) {
    const auto* p = bytes(r.next_hop);
    return std::all_of(p, p + (r.next_hop.native.si_family == AF_INET ? 4 : 16), [](unsigned char v) { return v == 0; });
}
}
