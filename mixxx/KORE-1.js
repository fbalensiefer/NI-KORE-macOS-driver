/* KORE 1 Mixxx development mapping; Mixxx >= 2.4.
 * Bridge MIDI input: KORE 1 Bridge
 * Bridge MIDI feedback sink: KORE 1 Feedback
 * Feedback CC channel 16 + custom 7D SysEx defined in kore1-final-bridge.c.
 */
var Kore1 = {};
Kore1.deck = 0; // 0=A 1=B
Kore1.connections = [];
Kore1.players = [];
Kore1.metadataConnections = [];
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
// Linux caiaq/input.c KORE key order: 37=backlight, 50=play, 51=stop.
Kore1.pressed = function(value,status) { return value>0 && (status&0xF0)!==0x80; };
Kore1.delta = function(value) { return value>0 && value<64?value:value>64 && value<128?64-value:0; };
Kore1.deckGroup = function() { return '[Channel'+(Kore1.deck+1)+']'; };
Kore1.unit = function() { return '[EffectRack1_EffectUnit'+(Kore1.deck+1)+']'; };
Kore1.pulse = function(group,key) { engine.setValue(group,key,1);engine.setValue(group,key,0); };
Kore1.button = function(channel,control,value,status) {
    // Esc is momentary cue; release must reach the deck it pressed on.
    if(control===41) {
        if(Kore1.pressed(value,status)) {
            if(!Kore1.cueHeld) {Kore1.cueHeld=Kore1.deckGroup();engine.setValue(Kore1.cueHeld,'cue_default',1);}
        } else if(Kore1.cueHeld) {engine.setValue(Kore1.cueHeld,'cue_default',0);Kore1.cueHeld=null;}
        return;
    }
    if(!Kore1.pressed(value,status))return;
    if(control===38 || control===42) {Kore1.deck=control===42?1:0;Kore1.updateHeader();return;} // Control=A / Sound=B
    var group=Kore1.deckGroup(),unit=Kore1.unit();
    if(control===50)script.toggleControl(group,'play');
    else if(control===51)engine.setValue(group,'play',0);
    else if(control===39) {if(!engine.getValue(group,'play'))Kore1.pulse(group,'LoadSelectedTrack');} // Enter: protect playing deck
    else if(control===48)script.toggleControl(group,'pfl'); // Listen
    else if(control===46 || control===45)engine.setValue('[Playlist]','SelectTrackKnob',control===46?-1:1);
    else if(control===47 || control===44) {
        var target=control===44?1:0,targetGroup='[Channel'+(target+1)+']';
        if(!engine.getValue(targetGroup,'play')) {
            Kore1.deck=target;Kore1.pulse(targetGroup,'LoadSelectedTrack');Kore1.updateHeader();
        }
    }
    else {
        // Softkeys left->right: 55,54,53,52 (top), 59,58,57,56 (bottom).
        var top=[55,54,53,52].indexOf(control),bottom=[59,58,57,56].indexOf(control);
        if(top===0)script.toggleControl('[QuickEffectRack1_'+group+']','enabled');
        else if(top>0)script.toggleControl(unit.slice(0,-1)+'_Effect'+top+']','enabled');
        else if(bottom===0)script.toggleControl(unit,'enabled');
        else if(bottom===1)script.toggleControl(unit,'group_'+group+'_enable');
        else if(bottom===2)Kore1.pulse(unit,'prev_chain_preset');
        else if(bottom===3)Kore1.pulse(unit,'next_chain_preset');
    }
    // Backlight (37), touch notes 60..67, menu and record deliberately unbound.
};
Kore1.encoder = function(channel,control,value) {
    var delta=Kore1.delta(value);if(!delta)return;
    var group=Kore1.deckGroup(),unit=Kore1.unit();
    var routes=[
        ['[QuickEffectRack1_'+group+']','super1'],
        [unit,'mix'],[unit,'super1'],
        [unit.slice(0,-1)+'_Effect1]','meta'],
        [unit.slice(0,-1)+'_Effect2]','meta'],
        [unit.slice(0,-1)+'_Effect3]','meta'],
        [unit.slice(0,-1)+'_Effect1]','parameter1'],
        [unit.slice(0,-1)+'_Effect2]','parameter1']
    ];
    var route=routes[control-20];if(!route)return;
    var current=engine.getParameter(route[0],route[1]);
    if(typeof current==='number' && isFinite(current))engine.setParameter(route[0],route[1],Kore1.clamp(current+delta/250,0,1));
};
Kore1.browse = function(channel,control,value) {
    var delta=Kore1.delta(value);if(delta)engine.setValue('[Playlist]','SelectTrackKnob',delta);
};
Kore1.init = function(id,debugging) {
    for(var d=0;d<2;d++) {
        (function(deck) {
            var g='[Channel'+(deck+1)+']';
            var p=typeof engine.getPlayer === "function" ? engine.getPlayer(g) : null;
            Kore1.players[deck]=p;
            if(p) {
                var titleCallback=function(t) { Kore1.sendText(deck,1,t); };
                var artistCallback=function(a) { Kore1.sendText(deck,2,a); };
                p.titleChanged.connect(titleCallback);
                p.artistChanged.connect(artistCallback);
                Kore1.metadataConnections.push([p.titleChanged,titleCallback]);
                Kore1.metadataConnections.push([p.artistChanged,artistCallback]);
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
    if(Kore1.cueHeld){engine.setValue(Kore1.cueHeld,'cue_default',0);Kore1.cueHeld=null;}
    for(var i=0;i<Kore1.connections.length;i++) Kore1.connections[i].disconnect();
    Kore1.connections=[];
    for(var j=0;j<Kore1.metadataConnections.length;j++) {
        var item=Kore1.metadataConnections[j];
        item[0].disconnect(item[1]);
    }
    Kore1.metadataConnections=[];
    Kore1.players=[];
};
