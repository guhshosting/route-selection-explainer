# route-selection-explainer 1.0.0

> Runtime prerequisite for the exact qualified EXE: Microsoft Visual C++ x64 Redistributable is required when the imported runtime DLLs are absent. This supersedes any static-CRT/no-redistributable description of a source build below.


> Release context: see [qualified platforms, runtime requirements and package scope](RELEASE-QUALIFICATION.md).


Native Windows C++17 CLI explaining the locally selected route and recommended
source for a numeric IPv4 or IPv6 destination. It queries configuration/state
without DNS, communication sockets, probes, or system changes. Normal use needs
no elevation. Human output uses Unicode console writes; redirected output is UTF-8.

```text
route-selection-explainer 8.8.8.8
route-selection-explainer 2001:4860:4860::8888 --json
route-selection-explainer --help
route-selection-explainer --version
```

Only numeric addresses accepted by `InetPtonW` are supported. Hostnames, ports,
brackets and whitespace are rejected. Version 1.0.0 rejects `%zone` syntax and
scope-dependent IPv6 (link/site-local unicast and non-global multicast) with an
explicit diagnostic; it never guesses an interface. Loopback, unspecified,
IPv4 link-local, multicast and broadcast-like addresses are passed to Windows;
special-use warnings describe interpretation limits. Directed broadcast status
is not inferred from a route alone.

## Selection and explanation

`GetBestRoute2` supplies the authoritative local selection with no caller-selected
interface or source. The source address is Windows' recommendation, not the first
adapter address or an independent implementation of IPv6 source selection.
`GetIpForwardTable2` supplies a separate snapshot of matching prefix evidence.
`GetIpInterfaceEntry` supplies the family-specific interface metric and
`GetIfEntry2` supplies alias, description, operational state and interface flags.
No adapter address enumeration is needed.

Longest matching prefix takes precedence over metrics among eligible routes.
The row's metric is a route metric **offset**; the combined preference metric is
that offset plus the interface metric for the same address family. A less-specific
route cannot overcome prefix preference by having a lower metric. Equal-prefix
comparisons report this sum, with caveats for eligibility and ties. Matching rows
are plausible competitors, not proof that all rows are eligible. The selected row
is included in `competing_routes` and marked `matches_selected` for context.
The explanation never overrides Windows' selected result. Unknown protocol,
origin and interface enum values remain numeric and are preserved.

An unspecified next hop means a direct/on-link route. A nonzero next hop identifies
the route's gateway. Hardware, loopback and tunnel classification uses Windows'
type and hardware flags; adapter names are never used to guess physical/virtual
status. A non-hardware flag alone is not treated as proof of a virtual adapter.

**A selected route does NOT prove that the destination, gateway, or network is reachable.**
This tool is an explainer, not a connectivity tester. It never pings, traces,
connects TCP, sends UDP/ICMP, performs DNS/HTTP, probes ARP/Neighbor Discovery,
tests gateways, captures packets or changes network configuration.

VPN, DHCP, route, interface and address changes can affect the result. Acquisition
is not an atomic transaction. A second best-route query warns about detected
changes but cannot prove stability. Missing interface metadata/metrics produce
warnings and nulls; the initial selection is retained. A failed table query
returns inspection failure while preserving any selected route.
If Windows already reported no route, a subsequent table failure is a warning
and retains the no-route status and error code. Malformed table
rows are skipped with warnings. No automatic retries or monitoring are performed.

Microsoft API references:
[GetBestRoute2](https://learn.microsoft.com/en-us/windows/win32/api/netioapi/nf-netioapi-getbestroute2),
[GetIpForwardTable2](https://learn.microsoft.com/en-us/windows/win32/api/netioapi/nf-netioapi-getipforwardtable2),
[route metric semantics](https://learn.microsoft.com/en-us/windows-hardware/drivers/network/mib-ipforward-row2),
[interface metric semantics](https://learn.microsoft.com/en-us/windows-hardware/drivers/network/mib-ipinterface-row).

## JSON and exit codes

`--json` outputs one clean JSON object on stdout, including errors. Schema version
`1.0` is described by [docs/json-schema.json](docs/json-schema.json). LUIDs are
decimal strings to preserve all 64 bits. Metrics and enum values are JSON numbers;
unavailable fields are null. `error.code` is a native Windows error for inspection
results, or the CLI exit code for input/internal failures. `factors` distinguishes
API selection from table evidence. Additive fields may be introduced in schema
1.x; incompatible changes require a new major schema version.
`table_evidence_complete` describes acquisition completeness, not atomicity or
eligibility. Human output limits evidence to 20 rows; JSON includes every match.

| Exit | Meaning |
|---|---|
| 0 | Successful selection/explanation (possibly warnings) |
| 2 | Usage error |
| 3 | Invalid or unsupported numeric destination |
| 4 | Windows reports no route (`ERROR_NOT_FOUND`, network/host unreachable) |
| 5 | Windows API inspection failure |
| 6 | Unexpected data/internal failure or output failure |

Other API errors, including unsupported stacks and access denial, are inspection
failures, not silently relabeled as no-route. Interface disappearance is a partial
metadata warning. `--help` and `--version` must be used alone.

## Build and test

Requires Windows 10/11 or corresponding supported Windows Server generations,
MSVC, Windows SDK, and CMake 3.24+. Primary host: Windows Server 2025 x64.
No external runtime libraries beyond the MSVC runtime and Windows system DLLs.
Compatibility is an API/build target; see [VALIDATION.md](VALIDATION.md) for actual
tested versions and limitations.

```text
cmake -S . -B build -A x64
cmake --build build --config Debug
cmake --build build --config Release
ctest --test-dir build -C Debug --output-on-failure
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix install
```

`RSE_ANALYZE=ON` enables MSVC `/analyze`; `/W4` is always enabled.
`RSE_ASAN=ON` enables MSVC AddressSanitizer. Use a separate build with
`CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=ProgramDatabase` for Debug ASan to avoid
Edit-and-Continue conflicts. ASan runtime DLLs must be available on PATH.
Clang-tidy and Cppcheck are independent developer checks, not dependencies.

The core separates parsing, CLI, result model, Windows acquisition, candidate
analysis, and human/JSON formatting. Synthetic tests exercise explanation only;
they never replace Windows selection. Integration tests compare route and source
against direct `GetBestRoute2` calls for documentation ranges and loopback in both
families. They neither require internet reachability nor send probes. Host changes
during comparison fail the test with a rerun diagnostic. A missing IPv6 route is
reported rather than treated as internet connectivity failure.
