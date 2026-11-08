const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync(process.argv[2], 'utf8');
const pointer = value => ({ toString: () => value, isNull: () => value === '0x0', readCString: () => value });
const attached = [];
let failAttach = false, failDetach = false, aliases = false;
const library = {
  name: 'libdl.so', base: pointer('0x1000'), size: 4096, path: '/system/lib64/libdl.so',
  findExportByName: name => pointer(name === 'dlopen' && !aliases ? '0x3000' : '0x2000'),
  enumerateExports: () => [{type: 'function', name: 'dlopen', address: pointer('0x3000')}]
};
const context = vm.createContext({
  Process: {id: 123, arch: 'arm64', enumerateModules: () => [library], findModuleByName: n => n === library.name ? library : null},
  Interceptor: { attach(address, handlers) {
    if (failAttach && attached.length === 1) throw Error('attach failed');
    const entry = {address, handlers, detached: false, detach() {
      if (failDetach) throw Error('detach failed');
      this.detached = true;
    }};
    attached.push(entry); return entry;
  }},
  rpc: {}, console: {log() {}}, send() {},
});
vm.runInContext(source, context);
async function main() {
  assert.equal(attached.length, 0, 'default must install no hooks');
  assert.equal(context.recon.modules()[0].path, library.path);
  assert.equal(context.rpc.exports.exports(library.name)[0].address, '0x3000');
  assert.throws(() => context.recon.exports('missing'), /not loaded/);
  await assert.rejects(context.recon.classes('Any'), /Java bridge unavailable/);
  context.Java = {available: true, perform: fn => fn(), enumerateLoadedClasses: c => {
    for (let i = 0; i < 1005; ++i) c.onMatch('Example.' + i);
    c.onMatch('Other'); c.onComplete();
  }};
  const limited = await context.recon.classes('^Example', 3);
  assert.equal(limited.matches.length, 3); assert.equal(limited.total, 1005); assert.equal(limited.truncated, true);
  assert.equal((await context.recon.classes('^Example', Infinity)).matches.length, 1000);
  assert.equal(context.recon.watchLoads(), 2); assert.equal(context.recon.watchLoads(), 2);
  assert.equal(attached.length, 2, 'watching is idempotent');
  failDetach = true; assert.throws(() => context.recon.stopWatchingLoads(), /detach failed/);
  failDetach = false; context.recon.stopWatchingLoads(); assert(attached.every(l => l.detached));
  attached.length = 0; failAttach = true;
  assert.throws(() => context.recon.watchLoads(), /attach failed/);
  assert(attached.every(l => l.detached), 'partial installation rolls back');
  failAttach = false; attached.length = 0; aliases = true;
  assert.equal(context.recon.watchLoads(), 1, 'aliased loader symbols are hooked once');
  context.recon.stopWatchingLoads();
}
main().catch(error => { console.error(error); process.exitCode = 1; });
