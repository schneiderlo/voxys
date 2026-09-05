import {test} from 'node:test';
import assert from 'node:assert/strict';
await import('../web/frame_budget.js');
const {summarize}=globalThis.VoxyFrameBudget;
const packet=(frame,time,extra={})=>({frame,gpu_frame_ms:time,frame_interval_available:true,
 render_width:1074,render_height:991,...extra});
test('uses envelope, not misleading stage sum',()=>{
 const result=summarize(Array.from({length:30},(_,i)=>packet(i,4,{total_ms:.5})));
 assert.equal(result.status,'sampled_budget_exceeded');assert.equal(result.p95_ms,4);
});
test('deduplicates asynchronous samples by their source frame',()=>{
 const a=packet(7,.8);assert.equal(summarize(Array(50).fill(a)).sample_count,1);
 assert.equal(summarize(Array(50).fill(a)).status,'insufficient_samples');
});
test('does not accept zeros, missing clocks, NaN or invalid dimensions',()=>{
 for(const a of [packet(0,0),packet(1,NaN),packet(2,.5,{frame_interval_available:false}),
  packet(3,.5,{render_width:0}),packet(4,.5,{render_height:2.5})])assert.equal(summarize([a]).sample_count,0);
});
test('does not combine resized frames into a budget pass',()=>{
 const a=Array.from({length:30},(_,i)=>packet(i,.2));a[29].render_width=400;
 assert.equal(summarize(a).status,'mixed_resolution');
});
test('p95 and p99 retain slow samples and nearest-rank boundaries',()=>{
 const a=Array.from({length:100},(_,i)=>packet(i,i<94?.7:9));const r=summarize(a);
 assert.equal(r.p95_ms,9);assert.equal(r.p99_ms,9);assert.equal(r.over_budget,6);
});
test('one millisecond means a measured sampled budget, not display FPS',()=>{
 const a=Array.from({length:30},(_,i)=>packet(i,.95));const r=summarize(a);
 assert.equal(r.status,'sampled_budget_met');assert.equal(r.target_ms,1);assert.equal(r.fps,undefined);
});
test('rejects invalid targets and sample thresholds',()=>{
 for(const v of [0,-1,Infinity,NaN])assert.throws(()=>summarize([],v),RangeError);
 assert.throws(()=>summarize([],1,1),RangeError);
});
test('report is a copy and input packets remain unchanged',()=>{
 const a=packet(0,1);const r=summarize([a]);r.samples[0].gpu_frame_ms=2;assert.equal(a.gpu_frame_ms,1);
});
