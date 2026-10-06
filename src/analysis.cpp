#include "rse.hpp"
#include <algorithm>
namespace rse {
bool same_route(const Route& a, const Route& b) {
    return same_address(a.prefix, b.prefix) && same_address(a.next_hop, b.next_hop) && a.length == b.length &&
        a.luid == b.luid && a.index == b.index && a.metric == b.metric && a.protocol == b.protocol && a.origin == b.origin && a.loopback == b.loopback;
}
std::optional<std::uint64_t> effective_metric(const Route& r) {
    if (!r.interface_metric) return std::nullopt;
    return static_cast<std::uint64_t>(r.metric) + *r.interface_metric;
}
void explain(Result& out, const std::vector<Route>& table) {
    out.candidates.clear(); out.factors.clear();
    out.factors.push_back(out.selected ? "The selected route and source address come from Windows GetBestRoute2, with no caller-specified interface or source address." : "Windows GetBestRoute2 returned no selected route; no caller-specified interface or source address was supplied.");
    out.factors.push_back("Longest matching prefix precedes metric preference. Among otherwise eligible routes of the same prefix length, preference uses route metric offset plus address-family-specific interface metric.");
    bool found = false, more_specific = false;
    for (const auto& r : table) {
        if (!matches(out.destination, r)) continue;
        Candidate c{r, "Matching prefix; eligibility is not inferred from the forwarding table alone.", false};
        if (out.selected) {
            const auto& s = *out.selected;
            c.selected = same_route(r, s); found |= c.selected;
            if (c.selected) c.factor = "Matches the Windows-selected route in this forwarding-table snapshot.";
            else if (r.length < s.length) c.factor = "Less specific than Windows-selected route; a lower metric cannot override longer-prefix preference among eligible routes.";
            else if (r.length > s.length) {
                more_specific = true;
                c.factor = "More specific than selected route: snapshots or route eligibility differ; do not infer Windows chose incorrectly.";
            } else {
                const auto rm = effective_metric(r), sm = effective_metric(s);
                if (!rm || !sm) c.factor = "Same prefix length; interface metric unavailable, so complete metric comparison is unavailable.";
                else if (*rm == *sm) c.factor = "Same prefix length and equal combined metric; Windows eligibility and tie-breaking are not reconstructed.";
                else if (*rm > *sm) c.factor = "Same prefix length and higher combined metric; consistent with metric preference if otherwise eligible.";
                else c.factor = "Same prefix length and lower combined metric; snapshots or eligibility differ, so metric alone does not explain the result.";
            }
        }
        out.candidates.push_back(std::move(c));
    }
    std::sort(out.candidates.begin(), out.candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.route.length != b.route.length) return a.route.length > b.route.length;
        if (a.route.index != b.route.index) return a.route.index < b.route.index;
        return address_text(a.route.next_hop) < address_text(b.route.next_hop);
    });
    if (out.selected) {
        if (!found && out.table_evidence_complete) out.warnings.push_back("Selected route is absent or differs in the forwarding-table snapshot; route state may have changed.");
        if (more_specific) out.warnings.push_back("More-specific matching table evidence exists; this snapshot cannot fully explain eligibility or selection.");
        else if (out.table_evidence_complete) out.factors.push_back("No more-specific matching route appears in the acquired forwarding-table snapshot.");
        if (!out.table_evidence_complete) out.factors.push_back("Forwarding-table evidence is incomplete or unavailable; absence of matching rows cannot establish absence of routes.");
        out.factors.push_back(on_link(*out.selected) ? "The selected next hop is unspecified: Windows reports a direct/on-link route." : "The selected route uses the reported next hop; its reachability was not tested.");
    } else out.factors.push_back("Windows returned no route; matching table rows, if any, do not establish an eligible route or usable source address.");
}
}
