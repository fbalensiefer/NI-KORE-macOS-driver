/* KORE 1 Mixxx v10 — proof-of-concept mapping; Mixxx >= 2.4.
 * Bridge MIDI input: KORE 1 Bridge
 * Bridge MIDI feedback sink: KORE 1 Feedback
 * Feedback CC channel 16 + custom 7D SysEx defined in kore-midi-v10.c.
 */
var Kore1 = {};
Kore1.deck = 0; // 0=A 1=B
Kore1.connections = [];
Kore1.players = [];
Kore1.clamp = function(v,lo,hi) { return Math.max(lo,Math.min(hi,v)); };
Kore1.sendCC = function(cc,value) {
    midi.sendShortMsg(0xBF,cc,Kore1.clamp(Math.round(value),0,127));
};
Kore1.sendText = function(deck,field,string) {
    var data=[0xF0,0x7D,0x4B,0x01,deck,field];
    string=String(string || '').substring(0,50);
    for(var i=0;i<string.length;i++) {
        var code=string.charCodeAt(i);
        data.push(code>=32 && code<=126 ? code : 63);
    }
    data.push(0xF7);
    midi.sendSysexMsg(data,data.length);
};
Kore1.updateHeader = function() {
    var g='[Channel'+(Kore1.deck+1)+']';
    var p=Kore1.players[Kore1.deck];
    Kore1.sendCC(40,Kore1.deck);
    Kore1.sendCC(41,engine.getValue(g,'play')>0?127:0);
    var bpm=Math.max(0,engine.getValue(g,'bpm')||0);
    var bpm10=Kore1.clamp(Math.round(bpm*10),0,16383);
    Kore1.sendCC(42,(bpm10>>7)&127);
    Kore1.sendCC(43,bpm10&127);
    Kore1.sendText(Kore1.deck,1,p ? p.title : '');
    Kore1.sendText(Kore1.deck,2,p ? p.artist : '');
    Kore1.sendCC(21,engine.getValue(g,'play')>0?127:0);
    Kore1.sendCC(23,engine.getValue(g,'play')>0?0:127);
};
Kore1.chooseDeck = function(channel,control,value,status,group) {
    if(value===0) return;
    Kore1.deck=(control===38)?1:0;
    Kore1.updateHeader();
};
Kore1.playButton = function(channel,control,value,status,group) {
    if(value===0) return;
    var deck=control===40?2:1;
    script.toggleControl('[Channel'+deck+']','play');
};
Kore1.cueButton = function(channel,control,value,status,group) {
    var deck=control===41?2:1;
    engine.setValue('[Channel'+deck+']','cue_default',value?1:0);
};
Kore1.encoder = function(channel,control,value,status,group) {
    var delta=value<=63?value:-(value-64);
    if(!delta) return;
    var routes=[
        ['[Channel1]','volume',0,1],
        ['[Channel2]','volume',0,1],
        ['[Channel1]','pregain',0,4],
        ['[Channel2]','pregain',0,4],
        ['[Channel1]','rate',-1,1],
        ['[Channel2]','rate',-1,1],
        ['[Master]','crossfader',-1,1],
        ['[Master]','gain',0,4]
    ];
    var r=routes[control-20];
    if(!r) return;
    var step=(r[3]-r[2])/1000;
    engine.setValue(r[0],r[1],Kore1.clamp(engine.getValue(r[0],r[1])+delta*step,r[2],r[3]));
};
Kore1.init = function(id,debugging) {
    for(var d=0;d<2;d++) {
        (function(deck) {
            var g='[Channel'+(deck+1)+']';
            var p=engine.getPlayer(g);
            Kore1.players[deck]=p;
            if(p) {
                p.titleChanged.connect(function(t) { Kore1.sendText(deck,1,t); });
                p.artistChanged.connect(function(a) { Kore1.sendText(deck,2,a); });
            }
            Kore1.connections.push(engine.makeConnection(g,'play',function(v) {
                if(deck===Kore1.deck) Kore1.updateHeader();
            }));
            Kore1.connections.push(engine.makeConnection(g,'bpm',function(v) {
                if(deck===Kore1.deck) Kore1.updateHeader();
            }));
        })(d);
    }
    Kore1.updateHeader();
};
Kore1.shutdown = function() {
    for(var i=0;i<Kore1.connections.length;i++) Kore1.connections[i].disconnect();
    Kore1.connections=[];
};
