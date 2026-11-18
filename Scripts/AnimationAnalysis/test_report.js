// Optional UI logic smoke test; no browser, server or npm dependency required.
// This checks plotting/control code, not visual layout or Unreal playback quality.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');

const text = fs.readFileSync(process.argv[2], 'utf8');
assert(!text.includes('__DATA__'), 'Report template was not populated');
const script = text.match(/<script>([\s\S]*?)<\/script>/)?.[1];
assert(script, 'Missing report script');
let draws = 0;
const context = new Proxy({}, {
    get(target, key) {
        if (key in target) return target[key];
        return (...args) => {
            for (const value of args) {
                if (typeof value === 'number') assert(Number.isFinite(value), `Non-finite canvas ${key}`);
            }
            draws++;
        };
    }
});
const ids = [...text.matchAll(/id="([^"]+)"/g)].map(match => match[1]);
const elements = Object.fromEntries(ids.map(id => [id, {
    value: '0', width: 1000, height: 600, textContent: '', getContext: () => context
}]));
let animation;
const sandbox = {
    document: { getElementById(id) { assert(id in elements, `Unknown element ${id}`); return elements[id]; } },
    requestAnimationFrame(callback) { animation = callback; }
};
vm.runInNewContext(script, sandbox, { timeout: 10000 });
assert(draws > 100, 'Initial pose/chart/heatmap were not drawn');
assert(elements.info.textContent.includes('샘플 0'));
elements.frame.value = '1';
elements.frame.oninput();
assert(elements.info.textContent.includes('샘플 1'));
elements.axis.value = '1';
elements.axis.onchange();
elements.play.onclick();
animation(1000);
animation(1100);
assert(Number(elements.frame.value) > 1, 'Playback did not advance');
elements.play.onclick();
elements.frame.value = elements.frame.max;
elements.frame.oninput();
assert(elements.info.textContent.includes('샘플 ' + elements.frame.max));
console.log(JSON.stringify({ passed: true, canvas_calls: draws, controls: ['seek', 'axis', 'play', 'final_frame'] }));
