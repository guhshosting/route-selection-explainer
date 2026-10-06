#include "rse.hpp"
#include <map>
#include <memory>
#include <stdexcept>
namespace rse {
std::string windows_error(unsigned code) {
    wchar_t buffer[1024]{};
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, buffer, 1024, nullptr);
    return "Windows error " + std::to_string(code) + (n ? ": " + utf8(std::wstring(buffer, n)) : " (no system description available)");
}
Route convert_route(const MIB_IPFORWARD_ROW2& row) {
    Route r;
    r.prefix.native = row.DestinationPrefix.Prefix; r.length = row.DestinationPrefix.PrefixLength;
    r.next_hop.native = row.NextHop;
    r.luid = row.InterfaceLuid.Value; r.index = row.InterfaceIndex;
    r.metric = row.Metric; r.protocol = static_cast<unsigned>(row.Protocol); r.origin = static_cast<unsigned>(row.Origin); r.loopback = row.Loopback != 0;
    if ((r.prefix.native.si_family != AF_INET && r.prefix.native.si_family != AF_INET6) ||
        r.length > (r.prefix.native.si_family == AF_INET ? 32u : 128u) || r.next_hop.native.si_family != r.prefix.native.si_family)
        throw std::runtime_error("Unexpected forwarding row: invalid family, next-hop family or prefix length");
    return r;
}
static std::optional<unsigned> acquire_metric(const Route& r, std::vector<std::string>& warnings, LocalApi& api) {
    MIB_IPINTERFACE_ROW row{}; InitializeIpInterfaceEntry(&row);
    row.Family = r.prefix.native.si_family; row.InterfaceLuid.Value = r.luid; row.InterfaceIndex = r.index;
    const auto e = api.ip_interface(row);
    if (e) { warnings.push_back("GetIpInterfaceEntry for index " + std::to_string(r.index) + " failed; interface metric unavailable: " + windows_error(e)); return std::nullopt; }
    return row.Metric;
}
static std::optional<Interface> acquire_interface(const Route& r, std::vector<std::string>& warnings, LocalApi& api) {
    // Windows output buffer: zero initialization is intentional; enum fields are filled on success.
    MIB_IF_ROW2 row{}; // NOLINT(bugprone-invalid-enum-default-initialization)
    row.InterfaceLuid.Value = r.luid; row.InterfaceIndex = r.index;
    const auto e = api.interface_info(row);
    if (e) { warnings.push_back("GetIfEntry2 failed; selected interface may have disappeared: " + windows_error(e)); return std::nullopt; }
    Interface i;
    i.luid = row.InterfaceLuid.Value; i.index = row.InterfaceIndex;
    i.alias = utf8(row.Alias); i.description = utf8(row.Description);
    i.oper_status = static_cast<unsigned>(row.OperStatus); i.type = row.Type; i.tunnel_type = static_cast<unsigned>(row.TunnelType);
    i.hardware = row.InterfaceAndOperStatusFlags.HardwareInterface != 0; i.metric = r.interface_metric;
    if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK) i.kind = "loopback";
    else if (row.Type == IF_TYPE_TUNNEL) i.kind = "tunnel";
    else if (*i.hardware) i.kind = "hardware interface (Windows flag)";
    else i.kind = "non-hardware interface (Windows flag; virtual/physical classification unspecified)";
    return i;
}
static bool no_route(unsigned e) { return e == ERROR_NOT_FOUND || e == ERROR_NETWORK_UNREACHABLE || e == ERROR_HOST_UNREACHABLE; }
class WindowsApi final : public LocalApi {
public:
    unsigned best(const Address& d, MIB_IPFORWARD_ROW2& r, SOCKADDR_INET& s) override { return GetBestRoute2(nullptr, 0, nullptr, &d.native, 0, &r, &s); }
    unsigned forwarding(ADDRESS_FAMILY f, PMIB_IPFORWARD_TABLE2& t) override { return GetIpForwardTable2(f, &t); }
    void release(PMIB_IPFORWARD_TABLE2 t) noexcept override { FreeMibTable(t); }
    unsigned ip_interface(MIB_IPINTERFACE_ROW& r) override { return GetIpInterfaceEntry(&r); }
    unsigned interface_info(MIB_IF_ROW2& r) override { return GetIfEntry2(&r); }
};
Result inspect(const Address& destination) { WindowsApi api; return inspect(destination, api); }
Result inspect(const Address& destination, LocalApi& api) {
    Result result; result.destination = destination; result.table_evidence_complete = false;
    MIB_IPFORWARD_ROW2 best{}; // NOLINT(bugprone-invalid-enum-default-initialization): Windows output buffer
    SOCKADDR_INET source{};
    const auto e = api.best(destination, best, source);
    if (e) {
        result.status = no_route(e) ? "no_route" : "inspection_error";
        result.error_code = e; result.error = "GetBestRoute2: " + windows_error(e);
        if (!no_route(e)) return result;
    } else {
        result.selected = convert_route(best);
        if (source.si_family != destination.native.si_family) throw std::runtime_error("GetBestRoute2 returned an unexpected source family");
        result.source = Address{source};
    }
    PMIB_IPFORWARD_TABLE2 raw = nullptr;
    const auto table_error = api.forwarding(destination.native.si_family, raw);
    auto release = [&api](PMIB_IPFORWARD_TABLE2 t) { api.release(t); };
    const std::unique_ptr<MIB_IPFORWARD_TABLE2, decltype(release)> table(raw, release);
    result.table_evidence_complete = !table_error || table_error == ERROR_NOT_FOUND;
    if (table_error && table_error != ERROR_NOT_FOUND) {
        result.table_evidence_complete = false;
        if (result.status == "no_route") result.warnings.push_back("GetIpForwardTable2 failed after Windows reported no route: " + windows_error(table_error));
        else {
            result.status = "inspection_error"; result.error_code = table_error;
            result.error = "GetIpForwardTable2: " + windows_error(table_error);
        }
    }
    std::map<std::uint64_t, std::optional<unsigned>> metrics;
    auto metric = [&](Route& r) {
        auto it = metrics.find(r.luid);
        if (it == metrics.end()) it = metrics.emplace(r.luid, acquire_metric(r, result.warnings, api)).first;
        r.interface_metric = it->second;
    };
    if (result.selected) {
        metric(*result.selected);
        result.interface = acquire_interface(*result.selected, result.warnings, api);
    }
    std::vector<Route> routes;
    if (!table_error) {
        if (!table) throw std::runtime_error("GetIpForwardTable2 returned a null table on success");
        for (ULONG n = 0; n < table->NumEntries; ++n) {
            try {
                auto r = convert_route(table->Table[n]);
                if (matches(destination, r)) { metric(r); routes.push_back(r); }
            } catch (const std::runtime_error& ex) { result.table_evidence_complete = false; result.warnings.push_back(std::string("Skipped unexpected table data: ") + ex.what()); }
        }
    }
    explain(result, routes);
    if (table_error && table_error != ERROR_NOT_FOUND) result.warnings.push_back("Forwarding-table evidence unavailable; no table-based absence claim is reliable.");
    // A second query detects some (not all) changes; never replaces the original snapshot.
    if (result.selected) {
        MIB_IPFORWARD_ROW2 again{}; // NOLINT(bugprone-invalid-enum-default-initialization): Windows output buffer
        SOCKADDR_INET again_source{};
        const auto again_error = api.best(destination, again, again_source);
        if (again_error) result.warnings.push_back("Selection recheck failed; dynamic state may have changed: " + windows_error(again_error));
        else if (!same_route(*result.selected, convert_route(again)) || !result.source || !same_address(*result.source, Address{again_source}))
            result.warnings.push_back("Windows-selected route or source changed during inspection; reported result is the initial query snapshot.");
    }
    const auto text = address_text(destination);
    if (text == "0.0.0.0" || text == "::") result.warnings.push_back("Unspecified destination: the local API result is not a meaningful remote connectivity target.");
    const auto* b = reinterpret_cast<const unsigned char*>(&destination.native.Ipv4.sin_addr);
    if ((destination.native.si_family == AF_INET && (b[0] >= 224 || (b[0] == 169 && b[1] == 254))) ||
        (destination.native.si_family == AF_INET6 && destination.native.Ipv6.sin6_addr.u.Byte[0] == 0xff))
        result.warnings.push_back("Special-use destination (multicast, reserved/broadcast-like or link-local): route evidence alone does not describe all delivery semantics. Directed broadcast is not inferred.");
    return result;
}
}
