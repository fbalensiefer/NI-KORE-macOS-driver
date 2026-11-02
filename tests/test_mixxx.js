/* Offline controller behavior tests: no Mixxx or USB device is opened. */
const assert=require('assert'), fs=require('fs'), vm=require('vm');
const values=new Map(), sent=[], signals=[], connections=[];
function signal(){const callbacks=new Set();const s={connect:f=>callbacks.add(f),disconnect:f=>callbacks.delete(f),callbacks};signals.push(s);return s;}
const players=[0,1].map(()=>({title:'Test',artist:'Artist',titleChanged:signal(),artistChanged:signal()}));
const engine={getValue:(g,k)=>values.get(g+'/'+k)||0,setValue:(g,k,v)=>values.set(g+'/'+k,v),
 getPlayer:g=>players[g==='[Channel1]'?0:1],
 makeConnection:(g,k,f)=>{const c={disconnected:false,disconnect(){this.disconnected=true;}};connections.push(c);return c;}};
const context={engine,midi:{sendShortMsg:(...m)=>sent.push(m),sendSysexMsg:(m,n)=>{assert.equal(m.length,n);sent.push(m);}},
 script:{toggleControl:(g,k)=>engine.setValue(g,k,engine.getValue(g,k)?0:1)}};
vm.createContext(context);vm.runInContext(fs.readFileSync('mixxx/KORE-1.js','utf8'),context);
const k=context.Kore1;k.init();
k.encoder(0,20,63);assert.equal(engine.getValue('[Channel1]','volume'),0.063);
k.encoder(0,20,127);assert.equal(engine.getValue('[Channel1]','volume'),0);
engine.setValue('[Channel1]','volume',0.999);k.encoder(0,20,63);assert.equal(engine.getValue('[Channel1]','volume'),1);
k.playButton(0,37,100);assert.equal(engine.getValue('[Channel1]','play'),1);
k.playButton(0,37,0);assert.equal(engine.getValue('[Channel1]','play'),1);
k.cueButton(0,41,100);assert.equal(engine.getValue('[Channel2]','cue_default'),1);
k.cueButton(0,41,0);assert.equal(engine.getValue('[Channel2]','cue_default'),0);
k.chooseDeck(0,38,100);assert.equal(k.deck,1);
k.sendText(1,1,'é'.repeat(100));const text=sent[sent.length-1];assert.equal(text.length,57);assert(text.slice(6,-1).every(x=>x===63));
for(const msg of sent){assert(msg.every(x=>Number.isInteger(x)&&x>=0&&x<=255));if(msg.length===3){assert.equal(msg[0],0xbf);assert(msg[1]<128&&msg[2]<128);}}
k.shutdown();assert(connections.every(c=>c.disconnected));assert(signals.every(s=>s.callbacks.size===0));
// Metadata support is optional; basic controls must still initialize without it.
delete engine.getPlayer;k.init();k.shutdown();
console.log('Mixxx controls, cue release, feedback bounds and callback cleanup passed');
