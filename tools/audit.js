'use strict';
const fs = require('node:fs');
const assert = require('node:assert/strict');
const imports = fs.readFileSync('artifacts/imports.log', 'utf8');
for (const [dll, allowed] of [
  ['WS2_32.dll', ['InetNtopW', 'InetPtonW']],
  ['IPHLPAPI.DLL', ['FreeMibTable', 'GetBestRoute2', 'GetIfEntry2', 'GetIpForwardTable2', 'GetIpInterfaceEntry', 'InitializeIpInterfaceEntry']]
]) {
  const part = imports.split(dll)[1].split(/\r?\n\s+\S+\.dll/i)[0];
  const names = [...part.matchAll(/^\s+[0-9A-F]+ ([A-Za-z_][A-Za-z_0-9]+)\s*$/gm)].map(m => m[1]).sort();
  assert.deepEqual(names, allowed.sort(), `unexpected ${dll} imports`);
  console.log(`${dll}: ${names.join(', ')}`);
}
const forbidden = /\b(socket|WSASocket[AW]?|connect|WSAConnect|send|sendto|WSASend\w*|recv\w*|getaddrinfo|GetAddrInfo\w*|gethostbyname|DnsQuery\w*|Icmp\w*|SendARP|ResolveIpNetEntry2|CreateIp\w*|SetIp\w*|DeleteIp\w*|FlushIp\w*|WinHttp\w*|InternetOpen\w*|LoadLibrary\w*|GetProcAddress|CreateProcess\w*|ShellExecute\w*|system|_wsystem|popen|_popen)\s*\(/g;
for (const name of fs.readdirSync('src').filter(n => n.endsWith('.cpp'))) {
  const source = fs.readFileSync(`src/${name}`, 'utf8');
  assert.equal(source.match(forbidden), null, `communication/mutation/process API in ${name}`);
}
assert(!/\b(WINHTTP|WININET|DNSAPI)\.dll/i.test(imports));
console.log('Production source and executable direct-import audit passed; packet-level behavior was not observed.');
