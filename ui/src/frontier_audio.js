// Original procedural instruments: no remote assets or external music. The
// existing accepted-state poll advances this score; audio owns no timer.
export function installFrontierAudio(environment=globalThis){
    const doc=environment.document,Context=environment.AudioContext||environment.webkitAudioContext;
    let context=null,master=null,limiter=null,noiseBuffer=null,sequence=null,active=false,enabled=true,stopped=false;
    let mood='silent',playingMood='silent',pace=0,phrase=0,step=0,nextPhrase=0,nextWind=0,nextWildlife=0,nextStep=0,nextPulse=0,payoffUntil=0;
    const voices=new Set(),limit=48;
    const random=(low,high)=>low+Math.random()*(high-low);
    const gainTo=value=>{
        if(!master||!context)return;
        master.gain.cancelScheduledValues(context.currentTime);
        master.gain.setTargetAtTime(value,context.currentTime,.04);
    };
    const dispose=voice=>{
        if(!voices.delete(voice))return;
        for(const node of voice.nodes)try{node.disconnect();}catch{}
    };
    const fadeVoices=(category=null,duration=.12)=>{
        if(!context)return;
        for(const voice of voices){
            if(voice.retiring||(category&&voice.category!==category))continue;
            voice.retiring=true;
            try{
                voice.envelope.gain.cancelScheduledValues(context.currentTime);
                voice.envelope.gain.setTargetAtTime(.0001,context.currentTime,duration/4);
                voice.source.stop(context.currentTime+duration);
            }catch{dispose(voice);}
        }
    };
    const resetSchedule=()=>{
        const now=context?.currentTime||0;
        nextPhrase=now+.12;nextWind=now;nextWildlife=now+random(2,5);nextStep=now+.12;nextPulse=now;
    };
    const audible=()=>!stopped&&active&&enabled&&context?.state==='running'&&!doc?.hidden;
    const syncOutput=()=>{
        gainTo(active&&enabled&&!doc?.hidden ? .28 : 0);
        if(!active||!enabled||doc?.hidden){fadeVoices();playingMood='silent';resetSchedule();}
    };
    const unlock=event=>{
        if(stopped||!active||!enabled||!Context||event.repeat)return;
        try{
            if(!context){
                context=new Context({latencyHint:'interactive'});master=context.createGain();master.gain.value=.28;
                if(context.createDynamicsCompressor){
                    limiter=context.createDynamicsCompressor();limiter.threshold.value=-10;limiter.knee.value=8;
                    limiter.ratio.value=12;limiter.attack.value=.003;limiter.release.value=.2;
                    master.connect(limiter);limiter.connect(context.destination);
                }else master.connect(context.destination);
                // One reusable second of noise feeds finite wind, breath, grit
                // and percussion voices. Filters shape each instrument.
                noiseBuffer=context.createBuffer(1,context.sampleRate,context.sampleRate);
                const samples=noiseBuffer.getChannelData(0);
                for(let i=0;i<samples.length;i++)samples[i]=Math.random()*2-1;
                resetSchedule();
            }
            if(context.state==='suspended')context.resume()?.catch(()=>{});
        }catch{/* Audio failure never interrupts gameplay. */}
    };
    const voice=(source,{delay=0,duration=.2,amplitude=.2,attack=.008,category='effect',pan=0}={},filters=[])=>{
        if(voices.size>=limit){source.disconnect();for(const filter of filters)filter.disconnect();return;}
        const envelope=context.createGain(),start=context.currentTime+delay;
        // Smooth attacks and a real release avoid clicks, including long pads.
        envelope.gain.setValueAtTime(.0001,start);
        envelope.gain.exponentialRampToValueAtTime(Math.max(.0002,amplitude),start+Math.min(attack,duration*.45));
        if(duration>1)envelope.gain.exponentialRampToValueAtTime(Math.max(.0002,amplitude*.6),start+duration*.62);
        envelope.gain.exponentialRampToValueAtTime(.0001,start+duration);
        const nodes=[source,...filters,envelope];
        if(context.createStereoPanner){const panner=context.createStereoPanner();panner.pan.value=pan;nodes.push(panner);}
        for(let i=0;i<nodes.length-1;i++)nodes[i].connect(nodes[i+1]);
        nodes.at(-1).connect(master);
        const record={source,envelope,nodes,category,retiring:false};voices.add(record);
        source.onended=()=>dispose(record);source.start(start);source.stop(start+duration+.03);
    };
    const tone=(frequency,{end=frequency,type='sine',...options}={})=>{
        if(voices.size>=limit)return;
        const source=context.createOscillator(),start=context.currentTime+(options.delay||0);
        source.type=type;source.frequency.setValueAtTime(frequency,start);
        source.frequency.exponentialRampToValueAtTime(Math.max(20,end),start+(options.duration||.2));
        voice(source,options);
    };
    const noise=(frequency,{end=frequency,filter='lowpass',...options}={})=>{
        if(!noiseBuffer||voices.size>=limit)return;
        const source=context.createBufferSource(),shape=context.createBiquadFilter(),start=context.currentTime+(options.delay||0);
        source.buffer=noiseBuffer;source.loop=true;source.playbackRate.value=random(.8,1.2);
        shape.type=filter;shape.Q.value=.6;shape.frequency.setValueAtTime(frequency,start);
        shape.frequency.exponentialRampToValueAtTime(Math.max(40,end),start+(options.duration||.2));
        voice(source,options,[shape]);
    };
    const bell=(frequency,options={})=>{
        tone(frequency,{duration:1.6,amplitude:.065,...options});
        tone(frequency*2.002,{...options,duration:(options.duration||1.6)*.5,
            amplitude:(options.amplitude||.065)*.2,delay:(options.delay||0)+.008});
    };
    const play=event=>{
        if(!audible())return;
        const pitch=random(.92,1.08),pan=random(-.15,.15);
        try{
            switch(event){
            case 'gather':
                noise(1900,{duration:.12,amplitude:.32,pan});
                tone(190*pitch,{end:95,duration:.15,amplitude:.32,type:'triangle',pan});
                noise(4200,{delay:.065,duration:.16,amplitude:.12,filter:'highpass',pan});break;
            case 'place':
                tone(125*pitch,{end:70,duration:.18,amplitude:.42,type:'triangle'});
                noise(1200,{duration:.1,amplitude:.40});
                tone(260*pitch,{delay:.04,end:190,duration:.12,amplitude:.15,type:'triangle'});break;
            case 'remove':
                noise(2500,{end:450,duration:.2,amplitude:.30});
                tone(170*pitch,{end:85,duration:.16,amplitude:.20,type:'triangle'});break;
            case 'craft':
                noise(2600,{duration:.06,amplitude:.25});
                for(const [i,hz] of [293.66,369.99,440,587.33].entries())bell(hz,{delay:i*.12,duration:.8,amplitude:.16});break;
            case 'equip':
                noise(1700,{duration:.09,amplitude:.20});
                tone(740*pitch,{end:680,duration:.27,amplitude:.065});break;
            case 'dodge':noise(650,{end:3600,duration:.25,amplitude:.26,attack:.07,pan});break;
            case 'warn':
                tone(440,{end:587.33,duration:.16,amplitude:.15,type:'triangle',pan});
                noise(2100,{duration:.09,amplitude:.15,pan});break;
            case 'charge':
                tone(73.42,{end:110,duration:.6,amplitude:.23,type:'triangle',attack:.08});
                noise(300,{end:1600,duration:.65,amplitude:.22,attack:.14});break;
            case 'discover':
                bell(440,{duration:1.8,amplitude:.17,pan:-.18});bell(659.25,{delay:.17,duration:2.2,amplitude:.15,pan:.18});break;
            case 'recover':
                for(const [i,hz] of [220,293.66,440].entries())tone(hz,{delay:i*.12,duration:1.9,attack:.18,amplitude:.13});break;
            case 'hit':
                noise(2700,{end:700,duration:.15,amplitude:.55,pan});
                tone(125*pitch,{end:42,duration:.20,amplitude:.48,type:'triangle',pan});
                noise(7000,{duration:.04,amplitude:.12,filter:'highpass',pan});break;
            case 'hurt':
                noise(650,{end:170,duration:.3,amplitude:.42});
                tone(95,{end:43,duration:.35,amplitude:.40,type:'triangle'});break;
            case 'death':
                fadeVoices('ambient',.3);
                tone(146.83,{end:73.42,duration:1.8,amplitude:.20,attack:.1});
                tone(220,{delay:.15,end:110,duration:1.6,amplitude:.12,attack:.1});break;
            case 'beacon':case 'victory':{
                fadeVoices('ambient',.3);payoffUntil=context.currentTime+(event==='victory'?7:5);
                noise(450,{end:3800,duration:1.4,amplitude:.13,attack:.5});
                for(const [i,hz] of [146.83,220,293.66,369.99,440,587.33].entries()){
                    tone(hz,{delay:i*.15,duration:event==='victory'?5:3.8,amplitude:.14,attack:.3,pan:(i%2?.18:-.18)});
                    bell(hz*2,{delay:i*.15+.2,duration:2.8,amplitude:.09});
                }
                break;
            }
            }
        }catch{/* A removed output device must not affect accepted game state. */}
    };
    const schedule=()=>{
        if(!audible())return;
        const now=context.currentTime;
        if(playingMood!==mood){
            fadeVoices('ambient',.45);fadeVoices('step',.08);playingMood=mood;resetSchedule();
        }
        if(mood==='silent')return;
        // Schedule from the current time, never catch up after a stalled frame
        // or a hidden tab. Every voice has an explicit stop and is capped.
        if(now>=nextWind){
            noise(mood==='camp'?430:680,{end:random(350,850),duration:5,amplitude:.047,attack:1.4,category:'ambient',pan:random(-.35,.35)});
            nextWind=now+3.8;
        }
        if(now>=nextWildlife){
            if(mood==='camp'){
                for(let i=0;i<3;i++)noise(random(900,3700),{delay:i*random(.08,.25),duration:random(.025,.07),amplitude:random(.025,.055),category:'ambient',pan:random(-.4,.4)});
                nextWildlife=now+random(.8,1.8);
            }else if(mood==='explore'){
                const hz=random(1800,2700),pan=random(-.7,.7);
                tone(hz,{end:hz*1.35,duration:.15,amplitude:.022,category:'ambient',pan});
                tone(hz*1.18,{end:hz*.88,delay:.19,duration:.19,amplitude:.018,category:'ambient',pan});
                nextWildlife=now+random(7,14);
            }else nextWildlife=now+3;
        }
        if(mood==='combat'){
            if(now>=nextPulse&&now>=payoffUntil){
                tone(73.42,{end:55,duration:.3,amplitude:.085,type:'triangle',category:'ambient'});
                noise(560,{delay:.30,duration:.1,amplitude:.075,category:'ambient'});
                if(phrase++%3===0)tone(146.83,{duration:1.7,amplitude:.033,attack:.2,category:'ambient',pan:-.25});
                nextPulse=now+.83;
            }
        }else if(now>=nextPhrase&&now>=payoffUntil){
            const chords=[[146.83,220,369.99],[98,196,293.66],[123.47,185,293.66],[110,164.81,246.94]];
            const pentatonic=[293.66,329.63,369.99,440,493.88,587.33,659.25,739.99];
            const contour=[[0,2,4,3],[5,4,2,1],[2,3,5,4],[3,1,0,2],[4,5,7,5],[2,0,1,3]];
            const progression=Math.floor(phrase/2)%chords.length,shape=contour[phrase%contour.length];
            for(const [i,hz] of chords[progression].entries())tone(hz,{duration:8.8,amplitude:mood==='camp'?.032:.041,attack:1.6,category:'ambient',pan:(i-1)*.27});
            // Leave alternating phrases open. The melody changes contour and
            // chord voicing over a minute instead of repeating a short loop.
            if(phrase%3!==2)for(const [i,index] of shape.entries())bell(pentatonic[index],{
                delay:.4+i*(mood==='camp'?1.1:.9),duration:2.6,amplitude:mood==='camp'?.025:.034,category:'ambient',pan:(i%2?.22:-.22)});
            phrase++;nextPhrase=now+7.4;
        }
        if(pace>0&&now>=nextStep){
            const pan=step++%2? .12:-.12;
            noise(random(1100,2200),{duration:.075,amplitude:pace===2?.15:.10,category:'step',pan});
            tone(random(74,100),{end:48,duration:.1,amplitude:pace===2?.13:.085,type:'triangle',category:'step',pan});
            nextStep=now+(pace===2?.29:.43);
        }else if(pace===0)nextStep=now+.1;
    };
    const visibility=()=>{syncOutput();resetSchedule();};
    doc?.addEventListener('pointerdown',unlock,true);doc?.addEventListener('keydown',unlock,true);doc?.addEventListener('visibilitychange',visibility);
    return {
        update(state){
            if(stopped)return;
            active=state.frontier===true&&!state.failed&&state.ready!==false;
            const nextEnabled=state.audioEnabled!==false;
            mood=['explore','camp','combat'].includes(state.audioMood)?state.audioMood:'silent';
            pace=state.audioPace===2?2:state.audioPace===1?1:0;
            if(enabled!==nextEnabled){enabled=nextEnabled;syncOutput();}
            if(!active||doc?.hidden)syncOutput();
            else gainTo(enabled ? .28 : 0);
            const next=state.audioSequence;
            if(!active||(typeof next!=='string'&&!(typeof next==='number'&&Number.isFinite(next))))sequence=null;
            else if(next!==sequence){
                const previous=sequence;sequence=next;
                // Initial snapshots, muted observations and pre-gesture events
                // are consumed without replay when sound later becomes audible.
                if(previous!==null)play(state.audioEvent);
            }
            try{schedule();}catch{/* Unsupported audio output remains optional. */}
        },
        cleanup(){
            if(stopped)return;stopped=true;
            doc?.removeEventListener('pointerdown',unlock,true);doc?.removeEventListener('keydown',unlock,true);doc?.removeEventListener('visibilitychange',visibility);
            for(const voice of [...voices]){try{voice.source.stop();}catch{}dispose(voice);}
            try{master?.disconnect();limiter?.disconnect();context?.close()?.catch(()=>{});}catch{}
            noiseBuffer=null;
        },
    };
}
