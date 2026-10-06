#include "rse.hpp"
#include <iostream>
#include <stdexcept>
#include <limits>
namespace {
unsigned checks = 0;
void check(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
class FakeApi final : public rse::LocalApi {
public:
    unsigned best_error = 0, table_error = 0, metadata_error = 0, metric_error = 0, calls = 0;
    bool change = false, released = false;
    MIB_IPFORWARD_ROW2 row{}; // NOLINT(bugprone-invalid-enum-default-initialization): Windows buffer fixture
    MIB_IPFORWARD_TABLE2 table{}; // NOLINT(bugprone-invalid-enum-default-initialization): Windows buffer fixture
    FakeApi() {
        row.InterfaceIndex = 7; row.InterfaceLuid.Value = 7;
        row.DestinationPrefix.Prefix = rse::parse_address(L"0.0.0.0").native;
        row.NextHop = rse::parse_address(L"192.0.2.1").native; row.Metric = 10;
        table.NumEntries = 1; table.Table[0] = row;
    }
    unsigned best(const rse::Address&, MIB_IPFORWARD_ROW2& r, SOCKADDR_INET& s) override {
        ++calls; r = row; s = rse::parse_address(change && calls > 1 ? L"192.0.2.3" : L"192.0.2.2").native; return best_error;
    }
    unsigned forwarding(ADDRESS_FAMILY, PMIB_IPFORWARD_TABLE2& t) override { if (!table_error) t = &table; return table_error; }
    void release(PMIB_IPFORWARD_TABLE2) noexcept override { released = true; }
    unsigned ip_interface(MIB_IPINTERFACE_ROW& r) override { r.Metric = 20; return metric_error; }
    unsigned interface_info(MIB_IF_ROW2& r) override {
        wcscpy_s(r.Alias, L"以太网 – Réseau 🚀"); r.Type = IF_TYPE_SOFTWARE_LOOPBACK; r.OperStatus = IfOperStatusUp; return metadata_error;
    }
};
void acquisition_failures() {
    using namespace rse;
    const auto d = parse_address(L"203.0.113.10");
    FakeApi no; no.best_error = ERROR_NOT_FOUND; no.table_error = ERROR_NOT_FOUND;
    auto r = inspect(d, no); check(r.status == "no_route" && !r.selected, "no route acquisition");
    no.table_error = ERROR_ACCESS_DENIED;
    r = inspect(d, no); check(r.status == "no_route" && r.error_code == ERROR_NOT_FOUND && !r.table_evidence_complete, "table failure cannot erase authoritative no-route result");
    FakeApi denied; denied.best_error = ERROR_ACCESS_DENIED;
    r = inspect(d, denied); check(r.status == "inspection_error" && r.error_code == ERROR_ACCESS_DENIED && !r.table_evidence_complete, "API access failure");
    FakeApi gone; gone.metadata_error = ERROR_FILE_NOT_FOUND; gone.metric_error = ERROR_NOT_FOUND;
    r = inspect(d, gone); check(r.status == "ok" && !r.interface && r.selected && !r.selected->interface_metric && !r.warnings.empty(), "disappearing interface acquisition");
    check(gone.released, "table release");
    FakeApi failed_table; failed_table.table_error = ERROR_ACCESS_DENIED;
    r = inspect(d, failed_table); check(r.status == "inspection_error" && r.selected && !r.table_evidence_complete, "table failure retains Windows selection");
    for (const auto& factor : r.factors) check(factor.find("No more-specific") == std::string::npos, "incomplete evidence cannot establish absence");
    FakeApi changed; changed.change = true;
    r = inspect(d, changed); check(!r.warnings.empty() && r.source && address_text(*r.source) == "192.0.2.2", "source race preserves original snapshot");
    FakeApi route_gone; route_gone.table.NumEntries = 0;
    r = inspect(d, route_gone); check(!r.warnings.empty() && r.selected, "route disappeared between queries");
    FakeApi invalid_table; invalid_table.table.Table[0].DestinationPrefix.PrefixLength = 255;
    r = inspect(d, invalid_table); check(!r.table_evidence_complete && !r.warnings.empty(), "invalid table row degrades evidence");
    FakeApi unicode;
    r = inspect(d, unicode); check(r.interface && r.interface->alias == utf8(L"以太网 – Réseau 🚀") && r.interface->kind == "loopback", "Unicode metadata and reliable loopback classification");
}
// Compact fixture constructor; numeric positions are documented by parameter names.
rse::Route route(const wchar_t* prefix, unsigned length, unsigned metric, unsigned interface_metric, unsigned index = 1, const wchar_t* hop = L"0.0.0.0") { // NOLINT(bugprone-easily-swappable-parameters)
    rse::Route r; r.prefix = rse::parse_address(prefix); r.length = length; r.metric = metric; r.interface_metric = interface_metric;
    r.index = index; r.luid = index; r.next_hop = rse::parse_address(hop); return r;
}
void synthetic() {
    using namespace rse;
    for (const auto* bad : {L"example.com", L"1.2.3", L"999.1.1.1", L"1.2.3.4:80", L"", L" 8.8.8.8", L"fe80::1", L"fe80::1%12", L"ff02::1", L"[::1]", L"1::2::3"}) {
        bool rejected = false; try { (void)parse_address(bad); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "malformed/unsupported address accepted");
    }
    check(address_text(parse_address(L"2001:db8::1")) == "2001:db8::1", "IPv6 format");
    check(address_text(parse_address(L"::1")) == "::1", "loopback parse");
    check(address_text(parse_address(L"169.254.1.1")) == "169.254.1.1", "IPv4 link-local");
    check(address_text(parse_address(L"255.255.255.255")) == "255.255.255.255", "broadcast parse");
    check(address_text(parse_address(L"::")) == "::", "unspecified parse");
    auto d = parse_address(L"203.0.113.10");
    auto def = route(L"0.0.0.0", 0, 1, 1);
    auto specific = route(L"203.0.113.0", 24, 500, 500, 2);
    Result r; r.destination = d; r.selected = specific;
    explain(r, {def, specific});
    check(r.candidates.size() == 2 && r.candidates[0].selected, "longest prefix evidence");
    check(r.candidates[1].factor.find("Less specific") != std::string::npos, "prefix before metric");
    check(on_link(specific), "on-link");
    auto gateway = route(L"0.0.0.0", 0, 10, 30, 3, L"192.0.2.1");
    check(!on_link(gateway), "gateway");
    r.selected = gateway;
    auto high = route(L"0.0.0.0", 0, 1, 100, 4);
    auto tie = route(L"0.0.0.0", 0, 30, 10, 5);
    auto near_metric = route(L"0.0.0.0", 0, 9, 30, 6);
    explain(r, {gateway, high, tie, near_metric});
    check(r.candidates[1].factor.find("higher combined") != std::string::npos, "interface metric outweighs raw route metric");
    check(r.candidates[2].factor.find("equal combined") != std::string::npos, "equal metric");
    check(r.candidates[3].factor.find("lower combined") != std::string::npos, "near metric requires eligibility caveat");
    high.interface_metric.reset(); check(!effective_metric(high), "missing metric");
    high.metric = (std::numeric_limits<unsigned>::max)(); high.interface_metric = (std::numeric_limits<unsigned>::max)();
    check(effective_metric(high).value_or(0) == 8589934590ULL, "metric sum overflow");
    auto v6 = route(L"2001:db8:8000::", 33, 1, 1, 7, L"::");
    check(matches(parse_address(L"2001:db8:ffff::1"), v6), "IPv6 partial byte prefix");
    check(!matches(parse_address(L"2001:db8:7fff::1"), v6), "IPv6 partial byte mismatch");
    v6.length = 128; v6.prefix = parse_address(L"2001:db8::1");
    check(matches(v6.prefix, v6), "IPv6 host prefix");
    check(!matches(d, v6), "family mismatch");
    auto host = route(L"203.0.113.10", 32, 1, 1);
    check(matches(d, host), "IPv4 host prefix"); host.length = 33; check(!matches(d, host), "invalid prefix");
    r.selected = gateway; r.warnings.clear(); explain(r, {specific});
    check(r.warnings.size() == 2, "disappearing selected route and more-specific evidence");
    r.selected.reset(); r.status = "no_route"; explain(r, {});
    check(r.candidates.empty() && format_json(r).find("\"selected_route\":null") != std::string::npos, "no route");
    r.status = "ok"; r.selected = gateway; r.selected->protocol = 99999; r.selected->origin = 99998;
    Interface i; i.alias = utf8(L"以太网 – Réseau 🚀"); i.description = "quote\" slash\\ newline\n"; r.interface = i;
    r.source = parse_address(L"192.0.2.2");
    check(format_json(r).find("99999") != std::string::npos, "unknown protocol preserved");
    check(format_json(r).find("99998") != std::string::npos, "unknown origin preserved");
    check(format_json(r).find(i.alias) != std::string::npos, "Unicode alias");
    check(format_json(r).find("quote\\\"") != std::string::npos, "JSON escaping");
    r.interface.reset(); check(format_human(r).find("metadata unavailable") != std::string::npos, "disappearing interface");
    r.interface = i;
    auto loop = route(L"127.0.0.0", 8, 0, 1); loop.loopback = true;
    check(matches(parse_address(L"127.0.0.1"), loop), "loopback prefix");
    MIB_IPFORWARD_ROW2 invalid{}; // NOLINT(bugprone-invalid-enum-default-initialization): intentionally malformed fixture
    bool rejected = false;
    try { (void)convert_route(invalid); } catch (const std::runtime_error&) { rejected = true; }
    check(rejected, "unexpected table data");
    check(parse_cli({L"--json", L"8.8.8.8"}).json, "CLI JSON");
    for (const auto& args : std::vector<std::vector<std::wstring>>{{}, {L"--bad"}, {L"8.8.8.8", L"::1"}, {L"--help", L"--json"}}) {
        rejected = false; try { (void)parse_cli(args); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "CLI usage");
    }
    std::cout << format_json(r); // Independent JSON validator consumes this Unicode fixture.
}
void integration() {
    using namespace rse;
    for (const auto* text : {L"203.0.113.10", L"127.0.0.1", L"2001:db8::1", L"::1"}) {
        const auto address = parse_address(text);
        MIB_IPFORWARD_ROW2 raw{}; // NOLINT(bugprone-invalid-enum-default-initialization): Windows output buffer
        SOCKADDR_INET source{};
        const auto before = GetBestRoute2(nullptr, 0, nullptr, &address.native, 0, &raw, &source);
        const auto result = inspect(address);
        MIB_IPFORWARD_ROW2 after_raw{}; // NOLINT(bugprone-invalid-enum-default-initialization): Windows output buffer
        SOCKADDR_INET after_source{};
        const auto after = GetBestRoute2(nullptr, 0, nullptr, &address.native, 0, &after_raw, &after_source);
        if (before != after || (!before && (!same_route(convert_route(raw), convert_route(after_raw)) || !same_address(Address{source}, Address{after_source})))) {
            throw std::runtime_error("Host changed during integration comparison; rerun on stable state");
        }
        if (before) {
            check(!result.selected && result.error_code == before, "API error comparison");
            std::cerr << address_text(address) << ": API returned " << before << '\n';
        } else {
            check(result.selected && same_route(*result.selected, convert_route(raw)), "direct API selected route comparison");
            check(result.source && same_address(*result.source, Address{source}), "direct API source comparison");
            check(result.status == "ok", "inspection failed");
            if (!result.selected) throw std::runtime_error("Missing selected route");
            std::cerr << address_text(address) << ": selected interface " << result.selected->index << '\n';
        }
    }
}
}
// Test exceptions are caught below, including a catch-all; clang-tidy conservatively
// reports an indirect integration exception despite these handlers.
int main(int argc, char* argv[]) { // NOLINT(bugprone-exception-escape)
    try { if (argc == 2 && std::string(argv[1]) == "--integration") integration(); else { acquisition_failures(); synthetic(); }
        std::cerr << checks << " checks passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    catch (...) { std::cerr << "Unexpected test failure\n"; return 1; }
}
