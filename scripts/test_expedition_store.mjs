import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createHash,webcrypto} from 'node:crypto';
import api from '../web/expedition_store.js';
const fixture=JSON.parse(await readFile(new URL('../tests/fixtures/save_generation_v1.json',import.meta.url)));
const bytes=hex=>new Uint8Array(Buffer.from(hex,'hex'));
test('SVSG JavaScript and native use the same frozen full-width envelope',async()=>{
    const encoded=await api.encodeGeneration(fixture.world,fixture.generation,bytes(fixture.payloadHex),webcrypto);
    assert.equal(Buffer.from(encoded).toString('hex'),fixture.bytesHex);
    assert.equal(createHash('sha256').update(encoded).digest('hex'),fixture.sha256);
    const decoded=await api.decodeGeneration(encoded,fixture.world,webcrypto);
    assert.equal(String(decoded.generation),fixture.generation);assert.deepEqual(decoded.payload,bytes(fixture.payloadHex));
});
test('every truncated or bit-damaged envelope rejects',async()=>{
    const encoded=bytes(fixture.bytesHex);
    for(let cut=0;cut<encoded.length;cut++)await assert.rejects(api.decodeGeneration(encoded.subarray(0,cut),fixture.world,webcrypto));
    for(let at=0;at<encoded.length;at++){const bad=encoded.slice();bad[at]^=1;await assert.rejects(api.decodeGeneration(bad,fixture.world,webcrypto));}
    const bad=encoded.slice();bad[4]=2;await assert.rejects(api.decodeGeneration(bad,fixture.world,webcrypto),{code:'UnsupportedSchema'});
});
test('generation, identity and size bounds reject imprecise or invalid inputs',async()=>{
    for(const value of [1,Number.MAX_SAFE_INTEGER+1,'01','18446744073709551616',0n])
        await assert.rejects(api.encodeGeneration(fixture.world,value,new Uint8Array([1]),webcrypto),{code:'InvalidData'});
    await assert.rejects(api.encodeGeneration('0'.repeat(32),1n,new Uint8Array([1]),webcrypto),{code:'InvalidData'});
    await assert.rejects(api.encodeGeneration(fixture.world,1n,new Uint8Array(),webcrypto),{code:'InvalidData'});
    await assert.rejects(api.encodeGeneration(fixture.world,1n,new Uint8Array(api.maximumPayloadBytes+1),webcrypto),{code:'Capacity'});
});
test('async hashing owns input and returned payloads',async()=>{
    const input=bytes(fixture.payloadHex),encoding=api.encodeGeneration(fixture.world,fixture.generation,input,webcrypto);input.fill(9);
    const encoded=await encoding;assert.equal(Buffer.from(encoded).toString('hex'),fixture.bytesHex);
    const decoding=api.decodeGeneration(encoded,fixture.world,webcrypto);encoded.fill(3);
    const decoded=await decoding;assert.deepEqual(decoded.payload,bytes(fixture.payloadHex));
});
