(function(root,factory){
    const api=factory();if(typeof module==='object'&&module.exports)module.exports=api;else root.VoxyDesignLibrary=api;
})(globalThis,function(){
    'use strict';
    const maximumBytes=128*1024,maximumDesigns=32;
    function validName(value){
        return typeof value==='string'&&value===value.trim()&&value.length>0&&new TextEncoder().encode(value).length<=96
            &&!/[\u0000-\u001f\u007f\ud800-\udfff]/u.test(value);
    }
    function envelope(value){
        if(!value||Object.keys(value).sort().join(',')!=='blueprint,name,version'||value.version!==1||!validName(value.name)
            ||typeof value.blueprint!=='string'||value.blueprint.length<96||value.blueprint.length>maximumBytes*2
            ||value.blueprint.length%2||!/^[0-9a-f]+$/.test(value.blueprint))throw Error('This design file is invalid or incompatible.');
        return value;
    }
    function parseFile(text){
        if(typeof text!=='string'||text.length>maximumBytes*2+1024)throw Error('This design file is too large.');
        let value;try{value=JSON.parse(text);}catch{throw Error('This is not a valid design file.');}return envelope(value);
    }
    function createStore(environment,validate){
        let connection=null,closed=false;
        const opening=new Promise((resolve,reject)=>{
            if(!environment.indexedDB){reject(Error('Saved designs are unavailable in this browser. Export a file instead.'));return;}
            const request=environment.indexedDB.open('voxys-blueprints-v1',1);
            request.onupgradeneeded=()=>{const db=request.result;db.createObjectStore('designs',{keyPath:'id'});db.createObjectStore('backups',{keyPath:'id'});};
            request.onerror=()=>reject(request.error||Error('Saved designs could not be opened.'));
            request.onblocked=()=>reject(Error('Close another game tab and reload to open saved designs.'));
            request.onsuccess=()=>{connection=request.result;connection.onversionchange=()=>connection.close();if(closed)connection.close();resolve(connection);};
        });
        // UI observes the rejection when listing or writing; do not create an unhandled rejection during startup.
        opening.catch(()=>{});
        const transaction=async(mode,work)=>{
            const db=await opening;if(closed)throw Error('The design library is closed.');
            return new Promise((resolve,reject)=>{
                const tx=db.transaction(['designs','backups'],mode,{durability:mode==='readwrite'?'strict':'default'});let result,failure;
                const fail=error=>{failure=error;try{tx.abort();}catch{reject(error);}};
                tx.oncomplete=()=>resolve(result);
                tx.onerror=()=>{failure=tx.error||failure;};
                tx.onabort=()=>reject(failure||tx.error||Error('The save was interrupted. Your previous design is unchanged.'));
                try{work(tx,value=>{result=value;},fail);}catch(error){fail(error);}
            });
        };
        const check=row=>{
            envelope({version:1,name:row?.name,blueprint:row?.blueprint});
            if(typeof row.id!=='string'||!/^[a-z0-9-]{1,64}$/.test(row.id)||typeof row.revision!=='string'
                ||!/^[1-9][0-9]{0,19}$/.test(row.revision)||BigInt(row.revision)>18446744073709551615n)
                throw Error('This saved design record is damaged.');
            if(!validate(row.blueprint))throw Error('This design is damaged or needs unavailable parts.');
            return row;
        };
        return {
            list:()=>transaction('readonly',(tx,done,fail)=>{
                const request=tx.objectStore('designs').getAll(undefined,maximumDesigns+1);request.onsuccess=()=>{
                    if(request.result.length>maximumDesigns){fail(Error('The design library exceeds its size limit.'));return;}
                    done(request.result.sort((a,b)=>String(a.name).localeCompare(String(b.name))||String(a.id).localeCompare(String(b.id))));
                };
            }),
            read:id=>transaction('readonly',(tx,done,fail)=>{
                const request=tx.objectStore('designs').get(id);request.onsuccess=()=>{try{done(check(request.result));}catch(error){fail(error);}};
            }),
            write:(id,name,blueprint,expectedRevision=null,restore=false)=>transaction('readwrite',(tx,done,fail)=>{
                const designs=tx.objectStore('designs'),backups=tx.objectStore('backups');
                const request=designs.getAll(undefined,maximumDesigns+1);request.onsuccess=()=>{try{
                    const rows=request.result,current=rows.find(row=>row.id===id);
                    if(rows.length>maximumDesigns||(!current&&rows.length>=maximumDesigns))throw Error('The library is full (32 designs). Export or remove a design first.');
                    if((current?.revision??null)!==expectedRevision)throw Error('This design changed in another tab. Refresh the list before saving.');
                    if(expectedRevision!==null&&(!/^[1-9][0-9]{0,19}$/.test(expectedRevision)||BigInt(expectedRevision)>=18446744073709551615n))throw Error('This saved design has an invalid revision.');
                    const install=source=>{
                        const row={id,name:source.name,blueprint:source.blueprint,revision:String(BigInt(expectedRevision||'0')+1n)};
                        check(row);
                        if(rows.some(other=>other.id!==id&&other.name===row.name))throw Error('Choose a different design name.');
                        if(current){try{check(current);backups.put(current);}catch(error){if(!restore)throw error;}}
                        designs.put(row);done(row);
                    };
                    if(restore){
                        const backup=backups.get(id);backup.onsuccess=()=>{try{install(check(backup.result));}catch(error){fail(error);}};
                    }else install({name,blueprint});
                }catch(error){fail(error);}};
            }),
            remove:(id,expectedRevision)=>transaction('readwrite',(tx,done,fail)=>{
                const designs=tx.objectStore('designs'),request=designs.get(id);request.onsuccess=()=>{
                    if(request.result?.revision!==expectedRevision){fail(Error('This design changed in another tab. Refresh the list first.'));return;}
                    designs.delete(id);tx.objectStore('backups').delete(id);done(true);
                };
            }),
            close(){closed=true;connection?.close();}
        };
    }
    function install(engine,environment=globalThis){
        const document=environment.document,panel=document.getElementById('workshop-designs');if(!panel)return {tick(){},cleanup(){}};
        const list=document.getElementById('design-list'),name=document.getElementById('design-name'),status=document.getElementById('design-status');
        const file=document.getElementById('design-file'),buttons=Array.from(panel.querySelectorAll('button'));
        let state=null,busy=false,stopped=false,rows=[],selected=null,loaded=false;
        const call=(action,text='')=>engine.ccall('voxy_salvage_blueprint_action','string',['number','string'],[action,text]);
        const store=createStore(environment,hex=>call(3,hex)==='ok');
        const selectedRow=()=>rows.find(row=>row.id===list.value);
        const showError=error=>{status.textContent=error?.name==='QuotaExceededError'?'Storage is full. Your previous design is safe; export a file to keep this design.':String(error?.message||error);};
        const render=()=>{
            const w=state?.workshop,ready=Boolean(state?.ready&&w?.open&&!w.pending&&!state?.busy&&!state?.failed&&!busy&&!stopped);
            const clean=ready&&(!w.changed||w.brickTool),has=Boolean(selectedRow());
            for(const b of buttons){
                const action=b.dataset.designAction;
                b.disabled=!ready||(action==='save-new'&&!clean)||(action==='update'&&(!clean||!has))
                    ||(['load','rename','duplicate','restore','remove'].includes(action)&&!has)||(action==='export'&&!clean)||(action==='import'&&!clean);
            }
            name.disabled=list.disabled=!ready;file.disabled=!clean;
            panel.dataset.busy=String(busy);
        };
        const refresh=async()=>{
            rows=await store.list();if(stopped)return;
            const old=selected??list.value;list.replaceChildren();
            for(const row of rows){const option=document.createElement('option');option.value=row.id;option.textContent=validName(row.name)?row.name:'Damaged design';list.append(option);}
            list.value=rows.some(row=>row.id===old)?old:(rows[0]?.id||'');selected=list.value;
        };
        const run=async operation=>{
            if(busy||stopped)return;busy=true;status.textContent="Working…";render();
            try{await operation();}catch(error){if(!stopped)showError(error);}finally{busy=false;if(!stopped)render();}
        };
        const currentBytes=()=>{const value=call(1);if(!value)throw Error('Keep or cancel your changes before saving.');return value;};
        const chosenName=()=>{const value=name.value.trim();if(!validName(value))throw Error('Enter a short name on one line.');return value;};
        const put=async(rowName,hex,old=null)=>{
            const saved=await store.write(old?.id||environment.crypto.randomUUID(),rowName,hex,old?.revision??null);selected=saved.id;
            await refresh();if(!stopped)status.textContent=`Saved “${saved.name}” in this browser.`;
        };
        const handlers=new Map();
        for(const button of buttons){
            const handler=()=>{if(button.disabled)return;void run(async()=>{
                const action=button.dataset.designAction,chosen=selectedRow();
                if(action==='refresh'){await refresh();status.textContent='Saved designs refreshed.';return;}
                if(action==='save-new'){await put(chosenName(),currentBytes());return;}
                if(action==='update'){await put(chosenName(),currentBytes(),chosen);return;}
                if(action==='load'){
                    const row=await store.read(chosen.id);if(stopped)return;
                    if(call(2,row.blueprint)!=='ok')throw Error('This design could not be loaded. Check the workshop message.');
                    name.value=row.name;status.textContent=`Loaded “${row.name}”. Check the price, then Launch.`;return;
                }
                if(action==='duplicate'||action==='rename'){
                    const row=await store.read(chosen.id);await put(chosenName(),row.blueprint,action==='rename'?row:null);return;
                }
                if(action==='restore'){
                    const restored=await store.write(chosen.id,'','',chosen.revision,true);selected=restored.id;await refresh();status.textContent='Previous saved version restored.';return;
                }
                if(action==='remove'){
                    if(!environment.confirm(`Remove saved design “${chosen.name}” and its backup?`))return;
                    await store.remove(chosen.id,chosen.revision);selected=null;await refresh();status.textContent='Saved design removed.';return;
                }
                if(action==='export'){
                    const value={version:1,name:chosenName(),blueprint:currentBytes()};envelope(value);
                    const url=environment.URL.createObjectURL(new Blob([JSON.stringify(value)],{type:'application/json'}));
                    const link=document.createElement('a');link.href=url;link.download=value.name.replace(/[^a-z0-9_-]/gi,'_')+'.voxy-design.json';
                    document.body.append(link);link.click();link.remove();environment.setTimeout(()=>environment.URL.revokeObjectURL(url),1000);
                    status.textContent='Design export started. Check your downloads.';return;
                }
                if(action==='import'){file.click();}
            });};handlers.set(button,handler);button.addEventListener('click',handler);
        }
        const selection=()=>{selected=list.value;const row=selectedRow();if(row&&validName(row.name))name.value=row.name;render();};
        const imported=()=>{const source=file.files?.[0];if(!source)return;void run(async()=>{
            if(source.size>maximumBytes*2+1024)throw Error('This design file is too large.');
            const value=parseFile(await source.text());if(stopped)return;
            if(call(3,value.blueprint)!=='ok')throw Error('This design is damaged or needs unavailable parts.');
            await put(value.name,value.blueprint);name.value=value.name;
        }).finally(()=>{file.value='';});};
        list.addEventListener('change',selection);file.addEventListener('change',imported);
        return {
            tick(next){state=next;if(!loaded&&next.workshop?.open&&next.ready){loaded=true;void run(async()=>{await refresh();status.textContent=rows.length?'Choose a saved design, or name and save this boat.':'No saved designs yet. Name this boat to save it.';});}render();},
            cleanup(){stopped=true;store.close();for(const [b,h]of handlers)b.removeEventListener('click',h);list.removeEventListener('change',selection);file.removeEventListener('change',imported);}
        };
    }
    return {createStore,parseFile,validName,install};
});
