'use strict';
// Offline independent parser/validator for the keywords used by our schema.
const fs = require('node:fs');
const assert = require('node:assert/strict');
const {spawnSync} = require('node:child_process');
const [exe, tests, schemaPath] = process.argv.slice(2);
const schema = JSON.parse(fs.readFileSync(schemaPath, 'utf8'));
function validate(value, rule) {
  if (rule.$ref) return validate(value, schema.$defs[rule.$ref.split('/').pop()]);
  if (rule.anyOf) {
    assert(rule.anyOf.some(r => { try { validate(value, r); return true; } catch { return false; } }), 'anyOf');
  }
  if ('const' in rule) assert.deepEqual(value, rule.const);
  if (rule.enum) assert(rule.enum.some(v => v === value), 'enum');
  const type = value === null ? 'null' : Array.isArray(value) ? 'array' : typeof value;
  if (rule.type) assert([].concat(rule.type).some(t => t === type || (t === 'integer' && Number.isInteger(value))), 'type');
  if (typeof value === 'number') {
    if ('minimum' in rule) assert(value >= rule.minimum);
    if ('maximum' in rule) assert(value <= rule.maximum);
  }
  if (typeof value === 'string' && rule.pattern) assert(new RegExp(rule.pattern).test(value));
  if (type === 'object') {
    for (const key of rule.required || []) assert(Object.hasOwn(value, key), `missing ${key}`);
    for (const [key, child] of Object.entries(rule.properties || {})) if (Object.hasOwn(value, key)) validate(value[key], child);
  }
  if (type === 'array' && rule.items) for (const item of value) validate(item, rule.items);
}
function run(args, expected) {
  const p = spawnSync(exe, args, {encoding: 'utf8'});
  assert.equal(p.error, undefined);
  assert(expected.includes(p.status), `exit ${p.status}: ${p.stderr}`);
  assert.equal(p.stderr, '', 'JSON stderr must be clean');
  const data = JSON.parse(p.stdout); validate(data, schema);
  return data;
}
for (const args of [[], ['--bad'], ['8.8.8.8', '::1'], ['--json'], ['--help', '--json'], ['--version', '--json'], ['--json', '--json', '8.8.8.8']]) run(args.includes('--json') ? args : ['--json', ...args], [2]);
for (const ip of ['example.com', '999.1.2.3', 'fe80::1', 'fe80::1%6', 'ff02::1', '[::1]', '8.8.8.8:80', '1::2::3']) run(['--json', ip], [3]);
for (const ip of ['203.0.113.10', '127.0.0.1', '2001:db8::1', '::1', '0.0.0.0', '::', '169.254.1.1', '255.255.255.255', '224.0.0.1', 'ff0e::1']) {
  const data = run([ip, '--json'], [0, 4]);
  if (data.status === 'ok') {
    assert(data.selected_route); assert(data.source_address);
    const r = data.selected_route;
    if (r.interface_metric !== null) assert.equal(r.effective_metric, r.route_metric + r.interface_metric);
  } else assert.equal(data.selected_route, null);
}
const fixture = spawnSync(tests, [], {encoding: 'utf8'});
assert.equal(fixture.status, 0, fixture.stderr);
const data = JSON.parse(fixture.stdout); validate(data, schema);
assert.equal(data.interface.alias, '以太网 – Réseau 🚀');
assert.equal(data.interface.description, 'quote" slash\\ newline\n');
assert.equal(data.selected_route.protocol, 99999);
assert.equal(data.selected_route.origin, 99998);
console.log('Independent JSON/schema, Unicode and CLI exit checks passed');
