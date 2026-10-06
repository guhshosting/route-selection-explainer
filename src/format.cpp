#include "rse.hpp"
#include <sstream>
#include <iomanip>
namespace rse {
static std::string quote(const std::string& s) {
    std::ostringstream out; out << '"';
    for (const unsigned char c : s) {
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(c) << std::dec;
            else out << static_cast<char>(c);
        }
    }
    out << '"'; return out.str();
}
static std::string number(const std::optional<unsigned>& n) { return n ? std::to_string(*n) : "null"; }
static std::string metric_text(const Route& r) { const auto m = effective_metric(r); return m ? std::to_string(*m) : "null"; }
static std::string route_json(const Route& r) {
    return "{\"prefix\":" + quote(address_text(r.prefix)) + ",\"prefix_length\":" + std::to_string(r.length) +
        ",\"interface_luid\":" + quote(std::to_string(r.luid)) + ",\"interface_index\":" + std::to_string(r.index) +
        ",\"next_hop\":" + quote(address_text(r.next_hop)) + ",\"on_link\":" + (on_link(r) ? "true" : "false") +
        ",\"route_metric\":" + std::to_string(r.metric) + ",\"interface_metric\":" + number(r.interface_metric) +
        ",\"effective_metric\":" + metric_text(r) + ",\"protocol\":" + std::to_string(r.protocol) +
        ",\"origin\":" + std::to_string(r.origin) + ",\"loopback\":" + (r.loopback ? "true" : "false") + "}";
}
static std::string strings(const std::vector<std::string>& values) {
    std::string out = "["; bool first = true;
    for (const auto& v : values) { if (!first) out += ','; first = false; out += quote(v); }
    return out + "]";
}
std::string format_json(const Result& r) {
    std::string out = "{\"schema_version\":\"1.0\",\"application_version\":\"1.0.0\",\"status\":" + quote(r.status) +
        ",\"destination\":" + quote(address_text(r.destination)) + ",\"family\":" + quote(r.destination.native.si_family == AF_INET ? "IPv4" : "IPv6") +
        ",\"selected_route\":" + (r.selected ? route_json(*r.selected) : "null") + ",\"interface\":";
    if (r.interface) {
        const auto& i = *r.interface;
        out += "{\"luid\":" + quote(std::to_string(i.luid)) + ",\"index\":" + std::to_string(i.index) +
            ",\"alias\":" + quote(i.alias) + ",\"description\":" + quote(i.description) + ",\"kind\":" + quote(i.kind) +
            ",\"oper_status\":" + std::to_string(i.oper_status) + ",\"if_type\":" + std::to_string(i.type) +
            ",\"tunnel_type\":" + std::to_string(i.tunnel_type) + ",\"hardware_interface\":" +
            (i.hardware ? (*i.hardware ? "true" : "false") : "null") + ",\"metric\":" + number(i.metric) + "}";
    } else out += "null";
    out += ",\"source_address\":" + (r.source ? quote(address_text(*r.source)) : "null") + ",\"competing_routes\":[";
    bool first = true;
    for (const auto& c : r.candidates) {
        if (!first) out += ','; first = false;
        out += "{\"route\":" + route_json(c.route) + ",\"matches_selected\":" + (c.selected ? "true" : "false") + ",\"factor\":" + quote(c.factor) + "}";
    }
    out += "],\"table_evidence_complete\":" + std::string(r.table_evidence_complete ? "true" : "false") + ",\"factors\":" + strings(r.factors) + ",\"warnings\":" + strings(r.warnings) + ",\"limitations\":" + strings(r.limitations) +
        ",\"error\":" + (r.error.empty() ? "null" : "{\"code\":" + std::to_string(r.error_code) + ",\"message\":" + quote(r.error) + "}") + "}\n";
    return out;
}
std::string format_error_json(const std::string& status, const std::string& message, unsigned code) {
    return "{\"schema_version\":\"1.0\",\"application_version\":\"1.0.0\",\"status\":" + quote(status) +
        ",\"destination\":null,\"family\":null,\"selected_route\":null,\"interface\":null,\"source_address\":null,\"competing_routes\":[],\"table_evidence_complete\":false,\"factors\":[],\"warnings\":[],\"limitations\":[],\"error\":{\"code\":" + std::to_string(code) + ",\"message\":" + quote(message) + "}}\n";
}
static std::string oper(unsigned n) {
    switch (n) { case 1: return "up"; case 2: return "down"; case 3: return "testing"; case 4: return "unknown";
        case 5: return "dormant"; case 6: return "not present"; case 7: return "lower layer down"; default: return "unknown (" + std::to_string(n) + ")"; }
}
std::string format_human(const Result& r) {
    std::ostringstream out;
    out << "Destination: " << address_text(r.destination) << "\nFamily: " << (r.destination.native.si_family == AF_INET ? "IPv4" : "IPv6") << "\nStatus: " << r.status << "\n\n";
    if (r.selected) {
        const auto& s = *r.selected;
        out << "Windows selected (GetBestRoute2):\n  Interface: " << (r.interface ? r.interface->alias : "metadata unavailable")
            << "\n  Interface index: " << s.index << "\n  Interface LUID: " << s.luid;
        if (r.interface) out << "\n  Interface state: " << oper(r.interface->oper_status) << "\n  Interface kind: " << r.interface->kind;
        out << "\n  Route: " << address_text(s.prefix) << '/' << s.length << "\n  Next hop: " << address_text(s.next_hop)
            << (on_link(s) ? " (direct/on-link)" : " (gateway)") << "\n  Route metric offset: " << s.metric
            << "\n  Interface metric: " << (s.interface_metric ? std::to_string(*s.interface_metric) : "unavailable")
            << "\n  Combined metric (sum): " << (effective_metric(s) ? metric_text(s) : "unavailable")
            << "\n  Protocol / origin (Windows numeric enums): " << s.protocol << " / " << s.origin
            << "\n  Loopback route flag: " << (s.loopback ? "true" : "false")
            << "\n  Windows-recommended source address: " << (r.source ? address_text(*r.source) : "unavailable") << "\n";
    }
    if (!r.error.empty()) out << "Error: " << r.error << '\n';
    out << "\nExplanation:\n";
    for (const auto& f : r.factors) out << "  " << f << '\n';
    out << "\nMatching forwarding-table evidence (" << r.candidates.size() << " rows, including selected row if present):\n";
    size_t shown = 0;
    for (const auto& c : r.candidates) {
        if (shown++ == 20) { out << "  Additional matching rows omitted from human output; use --json for the complete evidence.\n"; break; }
        out << "  " << address_text(c.route.prefix) << '/' << c.route.length << " if=" << c.route.index
        << " next-hop=" << address_text(c.route.next_hop) << " route=" << c.route.metric << " interface="
        << (c.route.interface_metric ? std::to_string(*c.route.interface_metric) : "unavailable") << " combined=" << metric_text(c.route)
        << "\n    " << c.factor << '\n';
    }
    for (const auto& w : r.warnings) out << "Warning: " << w << '\n';
    for (const auto& l : r.limitations) out << "Limitation: " << l << '\n';
    return out.str();
}
}
