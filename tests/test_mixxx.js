/* Offline controller behavior tests: no Mixxx or USB device is opened. */
const assert=require('assert'), fs=require('fs'), vm=require('vm');
const values=new Map(), sent=[], signals=[], connections=[];
function signal(){const callbacks=new Set();const s={connect:f=>callbacks.add(f),disconnect:f=>callbacks.delete(f),callbacks};signals.push(s);return s;}
const players=[0,1].map(()=>({title:'Test',artist:'Artist',titleChanged:signal(),artistChanged:signal(),trackUnloaded:signal()}));
const engine={getValue:(g,k)=>values.get(g+'/'+k)||0,setValue:(g,k,v)=>values.set(g+'/'+k,v),
 getPlayer:g=>players[g==='[Channel1]'?0:1],
 getParameter:(g,k)=>values.get(g+'/'+k)||0,setParameter:(g,k,v)=>values.set(g+'/'+k,v),
 makeConnection:(g,k,f)=>{const c={group:g,key:k,callback:f,disconnected:false,disconnect(){this.disconnected=true;}};connections.push(c);return c;}};
const context={engine,midi:{sendShortMsg:(...m)=>sent.push(m),sendSysexMsg:(m,n)=>{assert.equal(m.length,n);sent.push(m);}},
 script:{toggleControl:(g,k)=>engine.setValue(g,k,engine.getValue(g,k)?0:1)}};
vm.createContext(context);vm.runInContext(fs.readFileSync('mixxx/KORE-1.js','utf8'),context);
const k=context.Kore1;k.init();
function lastLed(index){return sent.filter(m=>m.length===3 && m[0]===0xbf && m[1]===index).slice(-1)[0][2];}
for(let index of [10,14,2,6,9,13,1,5,25])assert.equal(lastLed(index),64);
assert.equal(lastLed(26),127);assert.equal(lastLed(31),8);
let count=sent.length;k.refreshLights();assert.equal(sent.length,count); // unchanged values are not retransmitted
engine.setValue('[Channel1]','pfl',1);connections.find(c=>c.group==='[Channel1]' && c.key==='pfl').callback(1);
assert.equal(lastLed(17),127);
engine.setValue('[Channel1]','pfl',0);connections.find(c=>c.group==='[Channel1]' && c.key==='pfl').callback(0);
assert.equal(lastLed(17),8);
engine.setValue('[EffectRack1_EffectUnit1_Effect1]','enabled',1);
connections.find(c=>c.group==='[EffectRack1_EffectUnit1_Effect1]' && c.key==='enabled').callback(1);
assert.equal(lastLed(12),127);

k.encoder(0,20,63);assert.equal(engine.getValue('[QuickEffectRack1_[Channel1]]','super1'),63/250);
k.encoder(0,20,127);assert.equal(engine.getValue('[QuickEffectRack1_[Channel1]]','super1'),0);
k.button(0,37,100,0x90);assert.equal(engine.getValue('[Channel1]','play'),0); // backlight
k.button(0,50,100,0x90);assert.equal(engine.getValue('[Channel1]','play'),1);
k.button(0,50,100,0x80);assert.equal(engine.getValue('[Channel1]','play'),1);
k.button(0,51,100,0x90);assert.equal(engine.getValue('[Channel1]','play'),0); // release velocity is not a press
k.button(0,41,100,0x90);assert.equal(engine.getValue('[Channel1]','cue_default'),1);
k.button(0,42,100,0x90);assert.equal(k.deck,1);
k.button(0,41,0,0x80);assert.equal(engine.getValue('[Channel1]','cue_default'),0); // release original deck
k.encoder(0,21,63);assert.equal(engine.getValue('[EffectRack1_EffectUnit2]','mix'),63/250);
engine.setValue('[EffectRack1_EffectUnit2]','mix',.999);k.encoder(0,21,63);assert.equal(engine.getValue('[EffectRack1_EffectUnit2]','mix'),1);
k.button(0,54,100,0x90);assert.equal(engine.getValue('[EffectRack1_EffectUnit2_Effect1]','enabled'),1);
k.button(0,37,100,0x90);k.button(0,60,100,0x90);assert.equal(engine.getValue('[Channel2]','play'),0);
k.browse(0,28,1);assert.equal(engine.getValue('[Playlist]','SelectTrackKnob'),1);
k.browse(0,28,65);assert.equal(engine.getValue('[Playlist]','SelectTrackKnob'),-1);
for(let value of [0,64,128,255])assert.equal(k.delta(value),0);
// Loading cannot replace a playing deck; pulses reset immediately on a stopped deck.
const calls=[];const set=engine.setValue;engine.setValue=(g,key,v)=>{calls.push([g,key,v]);set(g,key,v);};
engine.setValue('[Channel2]','play',1);k.button(0,39,100,0x90);assert(!calls.some(c=>c[1]==='LoadSelectedTrack'));
engine.setValue('[Channel2]','play',0);k.button(0,39,100,0x90);assert.deepEqual(calls.slice(-2),[['[Channel2]','LoadSelectedTrack',1],['[Channel2]','LoadSelectedTrack',0]]);
// Arrow direction determines destination, independent of the previously selected deck.
engine.setValue('[Channel1]','play',0);engine.setValue('[Channel2]','play',0);
k.deck=1;k.button(0,47,100,0x90);assert.equal(k.deck,0);
assert(calls.slice(-2).every(c=>c[0]==='[Channel1]' && c[1]==='LoadSelectedTrack'));
k.button(0,44,100,0x90);assert.equal(k.deck,1);
assert.deepEqual(calls.slice(-2),[['[Channel2]','LoadSelectedTrack',1],['[Channel2]','LoadSelectedTrack',0]]);
for(const [note,deck] of [[47,1],[44,2]]) {
    engine.setValue('[Channel'+deck+']','play',1);
    let before=calls.length;k.button(0,note,100,0x90);
    assert.equal(calls.length,before); // playing target untouched
    k.button(0,note,100,0x80);assert.equal(calls.length,before); // release never loads
}
k.sendText(1,1,'é'.repeat(100));const text=sent[sent.length-1];assert.equal(text.length,57);assert(text.slice(6,-1).every(x=>x===63));
for(const msg of sent){assert(msg.every(x=>Number.isInteger(x)&&x>=0&&x<=255));if(msg.length===3){assert.equal(msg[0],0xbf);assert(msg[1]<128&&msg[2]<128);}}
// Metadata changes are forwarded for inactive decks, cached, then cleared on unload.
players[0].title='A New Track';for(const fn of players[0].titleChanged.callbacks)fn(players[0].title);
let metadata=sent.filter(m=>m[0]===0xf0 && m[4]===0 && m[5]===1).slice(-1)[0];
assert.equal(String.fromCharCode(...metadata.slice(6,-1)),'A New Track');
players[0].title='';players[0].artist='';for(const fn of players[0].trackUnloaded.callbacks)fn();
metadata=sent.filter(m=>m[0]===0xf0 && m[4]===0 && m[5]===1).slice(-1)[0];assert.equal(metadata.length,7);
k.sendText(0,1,'A New Track');let previous=sent.length;k.sendText(0,1,'A New Track');assert.equal(sent.length,previous);
k.sendText(0,1,'Grüße');metadata=sent[sent.length-1];assert.equal(String.fromCharCode(...metadata.slice(6,-1)),'Gruesse');
k.shutdown();assert(connections.every(c=>c.disconnected));assert(signals.every(s=>s.callbacks.size===0));
// Metadata support is optional; basic controls must still initialize without it.
delete engine.getPlayer;engine.setValue('[Channel2]','track_loaded',1);k.init();
metadata=sent.filter(m=>m[0]===0xf0 && m[4]===1 && m[5]===1).slice(-1)[0];
assert.equal(String.fromCharCode(...metadata.slice(6,-1)),'TRACK LOADED');k.shutdown();
console.log('Mixxx controls, cue release, feedback bounds and callback cleanup passed');
