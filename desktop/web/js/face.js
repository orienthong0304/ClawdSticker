/* face.js — clawpet face renderer (canvas), ported from docs/v2/clawpet-face.html.
 *
 * Differences from the source page: (1) reads window.MOODS (canonical 20 ids),
 * (2) adds pupil:'gaze' (dark eyeball + catchlight that tracks gaze) and
 *     mouth:'talk', matching the current firmware face engine,
 * (3) no demo / no button wiring — Python drives state via window.setMood().
 * Exposes window.setMood(key) and runs its own rAF loop.
 */
(function () {
  const W = 284, H = 346;
  const cv = document.getElementById('face');
  let ctx = cv.getContext('2d');                 // reassignable: faceSnapshot() swaps it
  const DPR = 2; cv.width = W * DPR; cv.height = H * DPR; ctx.scale(DPR, DPR);
  const CXm = W / 2, CYe = H * 0.45;
  const root = document.documentElement;
  const MOODS = window.MOODS;

  let shownKey = 'idle', queuedKey = 'idle', target = MOODS.idle;
  let tphase = 1, tdir = 0;                     // blink-changeover: 1=open 0=closed
  const A = {eyeH:1, eyeW:1, gazeX:0, gazeY:0, lid:0, glow:.18, dim:0, r:127, g:233, b:255};
  let blink = 0, blinkT = 1.2, sacT = 2, sacX = 0, t0 = performance.now();

  function hex2rgb(h){return [parseInt(h.slice(1,3),16),parseInt(h.slice(3,5),16),parseInt(h.slice(5,7),16)];}
  function lerp(a,b,k){return a+(b-a)*k;}
  function clamp(x,a,b){return Math.max(a,Math.min(b,x));}
  function ss(x){x=clamp(x,0,1);return x*x*(3-2*x);}

  window.setMood = function (key) {
    if (!MOODS[key]) return;
    queuedKey = key;
    if (shownKey !== key && tdir >= 0) tdir = -1;
  };
  window.currentMood = function(){ return shownKey; };

  /* Render a single still frame of `key` to an offscreen canvas and return a PNG
   * data URL. Reuses the live draw() by temporarily swapping the render target
   * (ctx) and freezing A/target to the mood's resting pose. Synchronous, so no
   * rAF frame interleaves — safe to mutate and restore the shared state. Works
   * even when the window is hidden (doesn't depend on the animation loop). */
  window.faceSnapshot = function (key, scale) {
    const m = MOODS[key]; if (!m) return null;
    scale = scale || 2;
    const oc = document.createElement('canvas');
    oc.width = W * scale; oc.height = H * scale;
    const octx = oc.getContext('2d'); octx.scale(scale, scale);
    const liveCtx = ctx, liveTarget = target, liveA = Object.assign({}, A);
    ctx = octx; target = m;
    const tg = hex2rgb(m.color);
    A.eyeH = m.eyeH; A.eyeW = m.eyeW; A.gazeX = 0; A.gazeY = m.gazeY;
    A.lid = m.lid || 0; A.glow = (m.glow != null ? m.glow : .18); A.dim = m.dim || 0;
    A.r = tg[0]; A.g = tg[1]; A.b = tg[2];
    let url = null;
    try {
      draw(0, `rgb(${tg[0]},${tg[1]},${tg[2]})`, 1, 1, 0, 0, 1);
      url = oc.toDataURL('image/png');
    } catch (e) { url = null; }
    ctx = liveCtx; target = liveTarget; Object.assign(A, liveA);
    return url;
  };

  /* Render one full animation loop of `key` as `count` PNG data URLs sampled
   * across `loopSeconds`. Same renderer as the live face, but with deterministic
   * motion only (busy dots / talk mouth / orbit / sparkle / bob / shake all
   * derive from sin(t)) plus one synthetic blink per loop — random saccades and
   * random blinks are dropped so the loop is seamless. Used by the menu-bar tray
   * to animate the face while the menu is open. Returns an array (or null). */
  window.faceFrames = function (key, scale, count, loopSeconds) {
    const m = MOODS[key]; if (!m) return null;
    scale = scale || 2; count = count || 20; loopSeconds = loopSeconds || 2.2;
    const oc = document.createElement('canvas');
    oc.width = W * scale; oc.height = H * scale;
    const octx = oc.getContext('2d'); octx.scale(scale, scale);
    const liveCtx = ctx, liveTarget = target, liveA = Object.assign({}, A);
    ctx = octx; target = m;
    const tg = hex2rgb(m.color);
    A.eyeW = m.eyeW; A.gazeX = 0; A.gazeY = m.gazeY; A.lid = m.lid || 0;
    A.glow = (m.glow != null ? m.glow : .18); A.dim = m.dim || 0;
    A.r = tg[0]; A.g = tg[1]; A.b = tg[2];
    const col = `rgb(${tg[0]},${tg[1]},${tg[2]})`;
    const openish = (m.shape === 'open' || m.shape === 'wink');
    const out = [];
    for (let i = 0; i < count; i++) {
      const p = i / count, t = p * loopSeconds;
      const breathe = Math.sin(t * 1.6) * 0.012 + 1;
      const bob = m.bob ? Math.sin(t * 5.5) * 5 : 0;
      const shake = m.shake ? Math.sin(t * 38) * 3 : 0;
      const d = p - 0.5;
      const bs = openish ? (1 - 0.9 * Math.exp(-(d * d * 900))) : 1;  // one blink/loop
      A.eyeH = m.eyeH;
      try { draw(t, col, bs, breathe, bob, shake, 1); out.push(oc.toDataURL('image/png')); }
      catch (e) { /* skip frame */ }
    }
    ctx = liveCtx; target = liveTarget; Object.assign(A, liveA);
    return out.length ? out : null;
  };

  function frame(now) {
    const t = (now - t0) / 1000, dt = Math.min(.05, (now - (frame._l || now)) / 1000);
    frame._l = now;
    const k = 1 - Math.pow(0.001, dt);

    if (tdir !== 0) {
      tphase += tdir * dt / 0.13;
      if (tphase <= 0 && tdir < 0) { tphase = 0; shownKey = queuedKey; target = MOODS[shownKey]; tdir = 1; }
      if (tphase >= 1 && tdir > 0) { tphase = 1; tdir = 0; }
    }
    const tg = hex2rgb(target.color);
    A.eyeH = lerp(A.eyeH, target.eyeH, k); A.eyeW = lerp(A.eyeW, target.eyeW, k);
    A.gazeY = lerp(A.gazeY, target.gazeY, k); A.lid = lerp(A.lid, target.lid, k);
    A.glow = lerp(A.glow, (target.glow != null ? target.glow : .18), k);
    A.dim = lerp(A.dim, target.dim || 0, k);
    A.r = lerp(A.r, tg[0], k); A.g = lerp(A.g, tg[1], k); A.b = lerp(A.b, tg[2], k);

    blinkT -= dt;
    const openish = (target.shape === 'open' || target.shape === 'wink');
    if (blinkT <= 0 && openish && tdir === 0) { blink = 1; blinkT = 1.6 + Math.random() * 3.4; }
    if (blink > 0) { blink -= dt * 7; if (blink < 0) blink = 0; }
    const blinkScale = 1 - Math.sin(Math.min(1, blink) * Math.PI) * 0.92;

    sacT -= dt;
    if (sacT <= 0) {
      if (target.dart) { sacX = (Math.random()*2-1)*30; sacT = 0.2+Math.random()*0.2; }
      else if (target.wander) { sacX = (Math.random()*2-1)*26; sacT = 1+Math.random()*1.2; }
      else { sacX = (Math.random()*2-1)*7; sacT = 2.6+Math.random()*3; }
    }
    A.gazeX = lerp(A.gazeX, sacX, target.dart ? k : k*.6);

    const breathe = Math.sin(t*1.6)*0.012+1;
    const bob = target.bob ? Math.sin(t*5.5)*5 : 0;
    const shake = target.shake ? Math.sin(t*38)*3 : 0;
    const ease = ss(tphase);

    const col = `rgb(${A.r|0},${A.g|0},${A.b|0})`;
    root.style.setProperty('--accent', col);
    const glowEl = document.getElementById('glow');
    if (glowEl) glowEl.style.opacity = A.glow;
    draw(t, col, blinkScale, breathe, bob, shake, ease);
    requestAnimationFrame(frame);
  }

  function rr(x,y,w,h,r){r=Math.min(r,w/2,h/2);ctx.beginPath();
    ctx.moveTo(x+r,y);ctx.arcTo(x+w,y,x+w,y+h,r);ctx.arcTo(x+w,y+h,x,y+h,r);
    ctx.arcTo(x,y+h,x,y,r);ctx.arcTo(x,y,x+w,y,r);ctx.closePath();}
  function heart(cx,cy,s,a,c){ctx.save();ctx.globalAlpha=a;ctx.fillStyle=c;ctx.beginPath();
    ctx.moveTo(cx,cy+s*.9);ctx.bezierCurveTo(cx+s,cy+s*.1,cx+s*.55,cy-s*.7,cx,cy-s*.15);
    ctx.bezierCurveTo(cx-s*.55,cy-s*.7,cx-s,cy+s*.1,cx,cy+s*.9);ctx.fill();ctx.restore();}

  function drawEye(sh, idx, gx, cy, w, h, col, pupil, t) {
    if (sh === 'open') {
      rr(gx-w/2, cy-h/2, w, h, w*0.42); ctx.fill();
      if (A.lid > 0.02) { ctx.save(); ctx.shadowBlur=0; ctx.fillStyle='#04050a';
        ctx.fillRect(gx-w/2-2, cy-h/2-2, w+4, h*A.lid); ctx.restore(); }
      if (pupil === 'gaze') {
        const nx = clamp(A.gazeX/30,-1,1), ny = clamp(A.gazeY/18,-1,1);
        const pr = w*0.18, mx = Math.max(0, w/2-pr-3), my = Math.max(0, h/2-pr-3);
        const px = gx + nx*mx, py = cy + ny*my;
        ctx.save(); ctx.shadowBlur=0;
        ctx.fillStyle='#04050a'; ctx.beginPath(); ctx.arc(px,py,pr,0,7); ctx.fill();
        ctx.fillStyle='#fff'; ctx.beginPath(); ctx.arc(px-pr*0.36, py-pr*0.42, pr*0.42, 0, 7); ctx.fill();
        ctx.restore();
      }
      if (pupil === 'shock') { ctx.save(); ctx.shadowBlur=0; ctx.fillStyle='#04050a';
        ctx.beginPath(); ctx.arc(gx,cy,w*0.17,0,7); ctx.fill(); ctx.restore(); }
      if (pupil === 'orbit') { ctx.save(); ctx.shadowBlur=0; ctx.fillStyle='#04050a'; const a=t*3;
        ctx.beginPath(); ctx.arc(gx+Math.cos(a)*w*0.18, cy+Math.sin(a)*h*0.16, w*0.2, 0, 7); ctx.fill(); ctx.restore(); }
      if (pupil === 'cute') { ctx.save(); ctx.shadowBlur=0; ctx.fillStyle='#fff';
        ctx.beginPath(); ctx.arc(gx-w*0.14, cy-h*0.16, w*0.13, 0, 7); ctx.fill();
        ctx.globalAlpha=.7; ctx.beginPath(); ctx.arc(gx+w*0.16, cy+h*0.06, w*0.07, 0, 7); ctx.fill(); ctx.restore(); }
    } else if (sh === 'happy') { ctx.lineWidth=11; ctx.lineCap='round';
      ctx.beginPath(); ctx.moveTo(gx-26,cy+8); ctx.quadraticCurveTo(gx,cy-26,gx+26,cy+8); ctx.stroke();
    } else if (sh === 'closed') { ctx.lineWidth=10; ctx.lineCap='round';
      ctx.beginPath(); ctx.moveTo(gx-26,cy-2); ctx.quadraticCurveTo(gx,cy+16,gx+26,cy-2); ctx.stroke();
    } else if (sh === 'squint') { ctx.lineWidth=10; ctx.lineCap='round'; ctx.lineJoin='round';
      const d=idx===0?1:-1; ctx.beginPath();
      ctx.moveTo(gx-18*d,cy-16); ctx.lineTo(gx+14*d,cy); ctx.lineTo(gx-18*d,cy+16); ctx.stroke();
    } else if (sh === 'tired') { ctx.lineWidth=9; ctx.lineCap='round';
      ctx.beginPath(); ctx.moveTo(gx-24,cy); ctx.quadraticCurveTo(gx,cy+5,gx+24,cy); ctx.stroke();
    } else if (sh === 'spiral') { ctx.lineWidth=6; ctx.lineCap='round'; ctx.beginPath(); const ang=t*6;
      for (let a=0;a<Math.PI*3;a+=0.25){const r2=2+a*2.6;const px=gx+Math.cos(a+ang)*r2;const py=cy+Math.sin(a+ang)*r2;
        a===0?ctx.moveTo(px,py):ctx.lineTo(px,py);} ctx.stroke(); }
  }

  function drawBrows(lx, rx, topY, name, alpha, col, t) {
    if (name === 'none' || alpha <= 0.02) return;
    ctx.save(); ctx.globalAlpha=alpha; ctx.strokeStyle=col; ctx.shadowColor=col; ctx.shadowBlur=10;
    ctx.lineWidth = name==='tired'?5:7; ctx.lineCap='round';
    let iy, oy;
    if (name==='up'){iy=topY-9;oy=topY-9;}
    else if (name==='flat'){iy=topY;oy=topY;}
    else if (name==='focus'){iy=topY+7;oy=topY+2;}
    else if (name==='angry'){iy=topY+10;oy=topY-7;}
    else if (name==='worried'){iy=topY-10;oy=topY+5;}
    else if (name==='tired'){iy=topY+6;oy=topY+6;}
    else if (name==='wobble'){iy=topY+Math.sin(t*8)*4;oy=topY-Math.sin(t*8)*4;}
    else {iy=topY;oy=topY;}
    ctx.beginPath(); ctx.moveTo(lx-24,oy); ctx.lineTo(lx+16,iy); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(rx+24,oy); ctx.lineTo(rx-16,iy); ctx.stroke();
    ctx.restore();
  }

  function draw(t, col, blinkScale, breathe, bob, shake, ease) {
    ctx.clearRect(0,0,W,H); ctx.fillStyle='#04050a'; ctx.fillRect(0,0,W,H);
    ctx.save(); ctx.globalAlpha=1-A.dim;

    const gazePupil = target.pupil === 'gaze';
    const baseW = 58*A.eyeW, baseH = 78*A.eyeH*blinkScale*breathe*Math.max(0.06,ease);
    const gap = 70;
    const cy = CYe + bob + shake + (gazePupil ? 0 : A.gazeY);
    const lx = CXm-gap, rx = CXm+gap;
    const shiftX = gazePupil ? 0 : A.gazeX;
    ctx.shadowColor=col; ctx.shadowBlur=22; ctx.fillStyle=col; ctx.strokeStyle=col;

    const sh = target.shape;
    [[0,lx],[1,rx]].forEach(([idx,ex]) => {
      let s = sh; if (sh==='wink') s = idx===0?'happy':'open';
      drawEye(s, idx, ex+shiftX, cy, baseW, baseH, col, target.pupil, t);
    });

    drawBrows(lx+shiftX, rx+shiftX, cy-baseH/2-15, target.brow, ease*(1-A.dim), col, t);

    // mouth
    ctx.shadowBlur=14; ctx.lineWidth=7; ctx.lineCap='round'; const my=cy+baseH/2+30;
    ctx.globalAlpha=ease*(1-A.dim);
    const m = target.mouth;
    if (m==='smile'){ctx.beginPath();ctx.moveTo(CXm-22,my-6);ctx.quadraticCurveTo(CXm,my+13,CXm+22,my-6);ctx.stroke();}
    else if (m==='frown'){ctx.beginPath();ctx.moveTo(CXm-20,my+8);ctx.quadraticCurveTo(CXm,my-8,CXm+20,my+8);ctx.stroke();}
    else if (m==='flat'){ctx.beginPath();ctx.moveTo(CXm-15,my);ctx.lineTo(CXm+15,my);ctx.stroke();}
    else if (m==='o'){ctx.lineWidth=5;ctx.beginPath();ctx.arc(CXm,my,7,0,7);ctx.stroke();}
    else if (m==='grit'){ctx.lineWidth=5;rr(CXm-18,my-6,36,12,4);ctx.stroke();
      ctx.beginPath();ctx.moveTo(CXm-6,my-6);ctx.lineTo(CXm-6,my+6);ctx.moveTo(CXm+6,my-6);ctx.lineTo(CXm+6,my+6);ctx.stroke();}
    else if (m==='wave'){ctx.lineWidth=5;ctx.beginPath();ctx.moveTo(CXm-18,my);
      ctx.quadraticCurveTo(CXm-9,my-7,CXm,my);ctx.quadraticCurveTo(CXm+9,my+7,CXm+18,my);ctx.stroke();}
    else if (m==='talk'){const hh=4+(Math.sin(t*14)*0.5+0.5)*9;ctx.fillStyle=col;rr(CXm-13,my-hh/2,26,hh,7);ctx.fill();}
    ctx.globalAlpha=1-A.dim;

    // extras
    ctx.shadowBlur=14; const ex=target.extra;
    if (ex==='excl'){const yy=CYe-92+Math.sin(t*5.5)*4;ctx.fillStyle=col;
      rr(CXm-5,yy,10,30,5);ctx.fill();ctx.beginPath();ctx.arc(CXm,yy+44,6,0,7);ctx.fill();}
    if (ex==='busy'){for(let i=0;i<3;i++){const a=(Math.sin(t*4-i*0.7)+1)/2;
      ctx.globalAlpha=(1-A.dim)*(0.25+a*0.75);ctx.beginPath();ctx.arc(CXm-22+i*22,CYe+96,5,0,7);ctx.fill();}ctx.globalAlpha=1-A.dim;}
    if (ex==='dots'){for(let i=0;i<3;i++){const a=(Math.sin(t*4-i*0.8)+1)/2;
      ctx.globalAlpha=(1-A.dim)*(0.2+a*0.8);ctx.beginPath();ctx.arc(CXm-18+i*18,CYe-86,4.5,0,7);ctx.fill();}ctx.globalAlpha=1-A.dim;}
    if (ex==='wave'){for(let i=-1;i<=1;i++){const hh=8+(Math.sin(t*9+i*1.1)*0.5+0.5)*22;
      rr(CXm+i*16-4,CYe+110-hh,8,hh,4);ctx.fill();}}
    if (ex==='sparkle'){[[CXm-86,CYe-60,1.1],[CXm+92,CYe-30,.8],[CXm+70,CYe+78,1]].forEach((s,i)=>{
      const a=(Math.sin(t*3+i*1.7)+1)/2,r=s[2]*(3+a*4);ctx.globalAlpha=(1-A.dim)*a;ctx.beginPath();
      ctx.moveTo(s[0],s[1]-r);ctx.lineTo(s[0]+r*.4,s[1]);ctx.lineTo(s[0],s[1]+r);ctx.lineTo(s[0]-r*.4,s[1]);ctx.closePath();ctx.fill();});ctx.globalAlpha=1-A.dim;}
    if (ex==='globe'){ctx.save();ctx.lineWidth=2.5;ctx.beginPath();ctx.arc(CXm,CYe-86,11,0,7);ctx.stroke();
      const a=t*3;ctx.beginPath();ctx.arc(CXm+Math.cos(a)*11,CYe-86+Math.sin(a)*4,3,0,7);ctx.fill();ctx.restore();}
    if (ex==='warn'){const yy=CYe-104;ctx.save();ctx.fillStyle='#ffd23f';ctx.shadowColor='#ffd23f';
      ctx.beginPath();ctx.moveTo(CXm,yy);ctx.lineTo(CXm+15,yy+25);ctx.lineTo(CXm-15,yy+25);ctx.closePath();ctx.fill();
      ctx.fillStyle='#1a1300';rr(CXm-2,yy+8,4,9,2);ctx.fill();ctx.beginPath();ctx.arc(CXm,yy+21,2,0,7);ctx.fill();ctx.restore();}
    if (ex==='sweat'){ctx.save();ctx.fillStyle='#9fe6ff';ctx.shadowColor='#9fe6ff';
      const dy=(t*40)%40;ctx.globalAlpha=(1-A.dim)*(1-dy/40);
      ctx.beginPath();ctx.ellipse(rx+34,cy-20+dy,3.5,5,0,0,7);ctx.fill();ctx.restore();}
    if (ex==='pack'){ctx.save();for(let i=0;i<8;i++){const a=t*3+i*Math.PI/4,r=26;
      ctx.globalAlpha=(1-A.dim)*(0.25+0.75*((Math.sin(t*3-i*0.5)+1)/2));
      ctx.beginPath();ctx.arc(CXm+Math.cos(a)*r,CYe+Math.sin(a)*r,3,0,7);ctx.fill();}ctx.restore();ctx.globalAlpha=1-A.dim;}
    if (ex==='heart'){ctx.save();ctx.shadowBlur=0;ctx.fillStyle='#ff9ec7';ctx.globalAlpha=(1-A.dim)*0.5;
      ctx.beginPath();ctx.arc(lx+shiftX,cy+baseH/2+6,9,0,7);ctx.arc(rx+shiftX,cy+baseH/2+6,9,0,7);ctx.fill();ctx.restore();
      for(let i=0;i<3;i++){const f=((t*0.45+i*0.33)%1);heart(CXm+(i-1)*34,CYe-50-f*60,8-f*3,(1-f)*0.9*(1-A.dim),'#ff7ab8');}}
    if (ex==='zzz'){ctx.textAlign='center';
      [['z',CXm+78,CYe-30,14],['Z',CXm+96,CYe-54,20],['z',CXm+62,CYe-10,12]].forEach((z,i)=>{
        const f=((t*0.4+i*0.33)%1);ctx.globalAlpha=(1-A.dim)*(1-f)*0.9;ctx.font=`bold ${z[3]}px JetBrains Mono`;
        ctx.fillStyle=col;ctx.fillText(z[0],z[1],z[2]-f*16);});ctx.globalAlpha=1-A.dim;ctx.textAlign='start';}

    ctx.shadowBlur=0; ctx.fillStyle='#1c1f29';
    for (let i=-2;i<=2;i++){ctx.beginPath();ctx.arc(CXm+i*11,H-26,2.4,0,7);ctx.fill();}
    ctx.restore();
  }

  requestAnimationFrame(frame);
})();
