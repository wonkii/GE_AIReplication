"""Self-contained review report: no CDN, web server, or chart package required."""
import html
import json


def render(path, raw, analysis, metrics):
    wanted = set(sum(analysis['config']['groups'].values(), []))
    indices = [i for i, bone in enumerate(raw['bones']) if bone['name'] in wanted]
    data = {'times': metrics['times'].tolist(), 'rootSpeed': metrics['root_speed'].tolist(),
            'names': [raw['bones'][i]['name'] for i in indices],
            'heat': metrics['speed'][:, indices].T.tolist(),
            'parents': [b['parent'] for b in raw['bones']],
            'poses': [[joint['relative'] for joint in s['joints']] for s in raw['samples']],
            'assetTimes': [s['asset_time'] for s in raw['samples']],
            'sourceFrames': [s['source_frame'] for s in raw['samples']],
            'segments': analysis['segments'], 'windows': analysis['warp_windows']}
    payload = json.dumps(data, allow_nan=False).replace('<', '\\u003c').replace('&', '\\u0026')
    rows = ''.join('<tr><td>{id}</td><td>{start:.4f}–{end:.4f}</td><td>{asset_start:.4f}–{asset_end:.4f}</td>'
                   '<td>{label}</td><td>{evidence}</td></tr>'.format(**{**s, 'evidence': html.escape(json.dumps(s['evidence'], ensure_ascii=False))})
                   for s in analysis['segments'])
    content = TEMPLATE.replace('__ASSET__', html.escape(raw['asset'])).replace('__ROWS__', rows)
    content = content.replace('__WARNINGS__', html.escape('\n'.join(analysis['warnings'] + analysis['warp_rejection_reasons'])))
    content = content.replace('__HASH__', analysis['analysis_hash']).replace('__DATA__', payload)
    with path.open('x', encoding='utf-8') as stream:
        stream.write(content)


TEMPLATE = '''<!doctype html>
<html lang="ko"><meta charset="utf-8"><title>관절 움직임 · 구간 분석</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#101723;color:#e5edf7;font:15px/1.6 system-ui,sans-serif}
main{max-width:1200px;margin:36px auto;padding:0 24px}h1{font-size:28px;margin:0}h2{font-size:19px}
.muted{color:#a8b6c9}.badge{display:inline-block;background:#594320;color:#ffe2a3;padding:4px 12px;border-radius:20px}
section{background:#1a2535;border:1px solid #33445b;border-radius:12px;padding:20px;margin:18px 0}
canvas{display:block;width:100%;background:#121c2b;border-radius:6px}.grid{display:grid;grid-template-columns:320px 1fr;gap:20px}
button,select{padding:8px 16px;background:#33445b;color:white;border:0;border-radius:6px;cursor:pointer}input{width:100%}
table{width:100%;border-collapse:collapse;font-size:13px}td,th{padding:10px;text-align:left;border-bottom:1px solid #33445b}
td:last-child{max-width:420px;overflow-wrap:anywhere}pre{white-space:pre-wrap;overflow-wrap:anywhere}.scroll{overflow:auto}
@media(max-width:800px){.grid{grid-template-columns:1fr}}
</style><main>
<span class="badge">검토 전 초안 · 전투에 자동 적용되지 않음</span>
<h1>관절 움직임과 PELT 구간 분석</h1><p class="muted">__ASSET__</p>
<section><div class="grid"><div><canvas id="pose" width="600" height="620"></canvas>
<select id="axis"><option value="0">X–Z 투영</option><option value="1">Y–Z 투영</option></select>
<p class="muted">Root 이동을 제거한 포즈. 에디터의 최종 IK·블렌딩 결과와 다를 수 있습니다.</p></div>
<div><h2>프레임 탐색</h2><button id="play">재생 / 정지</button><input id="frame" type="range" min="0" step="1" value="0">
<p id="info"></p><canvas id="timeline" width="1000" height="230"></canvas>
<p class="muted">곡선: Root 평면 속도(cm/s) · 경계: PELT · 녹색 띠: Warp 검토 후보</p>
<pre id="phase"></pre></div></div></section>
<section><h2>관절별 움직임</h2><canvas id="heat" width="1100" height="600"></canvas>
<p class="muted">Root 기준 이동 속도(cm/s). 행마다 같은 색상 범위를 사용합니다. 회전 속도는 joint_metrics.csv에 별도 저장됩니다.</p></section>
<section><h2>구간과 분리 근거</h2><div class="scroll"><table><thead><tr><th>#</th><th>재생 시간(s)</th><th>에셋 시간(s)</th><th>분류</th><th>근거</th></tr></thead><tbody>__ROWS__</tbody></table></div></section>
<section><h2>해석과 적용 범위</h2><pre>__WARNINGS__</pre><p>Warp 후보가 없어도 분석 실패는 아닙니다. 기존 평면·직선 Warp 제약과 맞지 않는 이유를 확인하세요.</p>
<p class="muted">분석 식별값: __HASH__</p></section></main>
<script>
const d=__DATA__, frame=document.getElementById('frame'), axis=document.getElementById('axis');
frame.max=d.times.length-1;
const duration=d.times.at(-1), colors=['#5e94d6','#ae85d9','#e7ae62','#62c9af','#d98294'];
function plot(){
 const i=+frame.value,t=d.times[i],s=d.segments.find(s=>t>=s.start&&t<s.end)||d.segments.at(-1);
 document.getElementById('info').textContent=`샘플 ${i} · 재생 ${t.toFixed(4)}s · 에셋 ${d.assetTimes[i].toFixed(4)}s · 원본 프레임 ${d.sourceFrames[i].toFixed(2)}`;
 document.getElementById('phase').textContent=`구간 ${s.id}: ${s.label}\n`+JSON.stringify(s.evidence,null,2);
 const c=document.getElementById('pose'),p=c.getContext('2d');p.clearRect(0,0,c.width,c.height);
 const a=+axis.value,pose=d.poses[i];
 const zs=pose.map(v=>v[2]),xs=pose.map(v=>v[a]);
 const scale=Math.min(480/Math.max(100,Math.max(...xs)-Math.min(...xs)),540/Math.max(100,Math.max(...zs)-Math.min(...zs)));
 const x=v=>300+(v[a]-(Math.max(...xs)+Math.min(...xs))/2)*scale,y=v=>570-(v[2]-Math.min(...zs))*scale;
 p.strokeStyle=colors[s.id%colors.length];p.lineWidth=3;p.beginPath();
 pose.forEach((v,k)=>{const parent=d.parents[k];if(parent>=0){p.moveTo(x(v),y(v));p.lineTo(x(pose[parent]),y(pose[parent]));}});p.stroke();
 const chart=document.getElementById('timeline'),ctx=chart.getContext('2d');ctx.clearRect(0,0,1000,230);
 const tx=t=>60+t/duration*920,max=Math.max(1,...d.rootSpeed);
 ctx.fillStyle='#9eb0c8';ctx.font='16px system-ui';ctx.fillText(max.toFixed(1),4,30);ctx.fillText('0',28,185);
 d.segments.forEach(s=>{ctx.fillStyle=colors[s.id%colors.length]+'33';ctx.fillRect(tx(s.start),15,tx(s.end)-tx(s.start),175);ctx.strokeStyle='#899db8';ctx.beginPath();ctx.moveTo(tx(s.start),15);ctx.lineTo(tx(s.start),190);ctx.stroke();});
 ctx.strokeStyle='#80c7ff';ctx.lineWidth=2;ctx.beginPath();d.rootSpeed.forEach((v,k)=>{const x=tx((d.times[k]+d.times[k+1])/2),y=185-v/max*155;k?ctx.lineTo(x,y):ctx.moveTo(x,y)});ctx.stroke();
 ctx.fillStyle='#62c9af';d.windows.forEach(w=>ctx.fillRect(tx(w.start),202,tx(w.end)-tx(w.start),12));
 ctx.strokeStyle='#fff';ctx.beginPath();ctx.moveTo(tx(t),10);ctx.lineTo(tx(t),220);ctx.stroke();
 heat(t);
}
function heat(t){
 const c=document.getElementById('heat'),ctx=c.getContext('2d'),h=22;c.height=d.names.length*h+40;
 const max=d.heat.reduce((peak,row)=>row.reduce((p,v)=>Math.max(p,v),peak),1), left=160,width=920;
 d.heat.forEach((row,j)=>{ctx.fillStyle='#d7e4f5';ctx.font='13px system-ui';ctx.fillText(d.names[j],4,j*h+17);
 row.forEach((v,k)=>{const f=Math.sqrt(v/max);ctx.fillStyle=`rgb(${Math.round(20+235*f)},${Math.round(40+130*f)},${Math.round(65-30*f)})`;ctx.fillRect(left+d.times[k]/duration*width,j*h,(d.times[k+1]-d.times[k])/duration*width+1,h-1);});});
 ctx.strokeStyle='white';ctx.beginPath();ctx.moveTo(left+t/duration*width,0);ctx.lineTo(left+t/duration*width,d.names.length*h);ctx.stroke();
 ctx.fillStyle='#a8b6c9';ctx.fillText(`0 → ${max.toFixed(1)} cm/s`,left,c.height-8);
}
frame.oninput=plot;axis.onchange=plot;let playing=false,last=0,elapsed=0;
document.getElementById('play').onclick=()=>{playing=!playing;last=0;elapsed=d.times[+frame.value];};
function tick(now){if(playing){if(last)elapsed+=(now-last)/1000;if(elapsed>duration)elapsed=0;let i=d.times.findIndex(t=>t>=elapsed);frame.value=Math.max(0,i);plot();last=now;}requestAnimationFrame(tick)}
plot();requestAnimationFrame(tick);
</script></html>'''
