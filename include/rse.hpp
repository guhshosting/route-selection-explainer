#ifndef RSE_HPP_INCLUDED
#define RSE_HPP_INCLUDED
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rse {
constexpr const char* version = "1.0.0";
struct Address { SOCKADDR_INET native{}; };
struct Route {
    Address prefix, next_hop;
    unsigned length = 0;
    std::uint64_t luid = 0;
    unsigned index = 0, metric = 0, protocol = 0, origin = 0;
    bool loopback = false;
    std::optional<unsigned> interface_metric;
};
struct Interface {
    std::uint64_t luid = 0;
    unsigned index = 0;
    std::string alias, description, kind;
    unsigned oper_status = 0, type = 0, tunnel_type = 0;
    std::optional<bool> hardware;
    std::optional<unsigned> metric;
};
struct Candidate { Route route; std::string factor; bool selected = false; };
struct Result {
    std::string status = "ok";
    Address destination;
    std::optional<Route> selected;
    std::optional<Interface> interface;
    std::optional<Address> source;
    bool table_evidence_complete = true;
    std::vector<Candidate> candidates;
    std::vector<std::string> factors, warnings;
    std::vector<std::string> limitations{
        "Selection and inspection are separate local snapshots; VPN, DHCP, route, interface and address changes can alter the result.",
        "A selected route does NOT prove that the destination, gateway, or network is reachable.",
        "Candidates are route-table evidence, not an authoritative reconstruction of Windows eligibility, source selection or tie-breaking.",
        "No DNS, communication sockets or connectivity probes are used. Packet-level absence requires external observation."};
    unsigned error_code = 0;
    std::string error;
};
struct Options { bool json = false, help = false, show_version = false; std::wstring destination; };
Options parse_cli(const std::vector<std::wstring>& args);
Address parse_address(const std::wstring& text);
std::string address_text(const Address& address);
std::string utf8(const std::wstring& text);
bool matches(const Address& destination, const Route& route);
bool same_address(const Address& a, const Address& b);
bool same_route(const Route& a, const Route& b);
bool on_link(const Route& route);
std::optional<std::uint64_t> effective_metric(const Route& route);
Route convert_route(const MIB_IPFORWARD_ROW2& row);
void explain(Result& result, const std::vector<Route>& table);
Result inspect(const Address& destination);
// Injectable acquisition boundary for deterministic failure/race tests.
class LocalApi {
public:
    virtual ~LocalApi() = default;
    virtual unsigned best(const Address&, MIB_IPFORWARD_ROW2&, SOCKADDR_INET&) = 0;
    virtual unsigned forwarding(ADDRESS_FAMILY, PMIB_IPFORWARD_TABLE2&) = 0;
    virtual void release(PMIB_IPFORWARD_TABLE2) noexcept = 0;
    virtual unsigned ip_interface(MIB_IPINTERFACE_ROW&) = 0;
    virtual unsigned interface_info(MIB_IF_ROW2&) = 0;
};
Result inspect(const Address& destination, LocalApi& api);
std::string format_json(const Result& result);
std::string format_human(const Result& result);
std::string format_error_json(const std::string& status, const std::string& message, unsigned code);
std::string windows_error(unsigned code);
}
#endif
