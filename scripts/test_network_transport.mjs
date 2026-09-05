import {test} from 'node:test';
import assert from 'node:assert/strict';

await import('../web/network_transport.js');
const {
    REALTIME, RELIABLE_EVENT, RELIABLE_CONTROL,
    RealtimeGateway, WebTransportEndpoint, WebRtcDataChannelEndpoint,
} = globalThis.VoxyNetworkTransport;

const invalidFrames = [
    [REALTIME, new Uint8Array(1201)],
    [REALTIME, new Uint8Array(0)],
    [RELIABLE_EVENT, new Uint8Array(0)],
    [RELIABLE_CONTROL, new Uint8Array(16 * 1024 * 1024 + 1)],
    [99, new Uint8Array(1)],
];

test('invalid frames cannot close a healthy primary or trigger failover', async () => {
    const gateway = new RealtimeGateway();
    let writes = 0;
    let closes = 0;
    let connects = 0;
    gateway.active = gateway.webTransport;
    gateway.webTransport.send = async () => { ++writes; return false; };
    gateway.webTransport.close = () => { ++closes; };
    gateway.webRtc.connect = async () => { ++connects; return true; };
    gateway.webRtc.send = async () => { ++writes; return true; };
    for (const [channel, payload] of invalidFrames) {
        assert.equal(await gateway.send(channel, payload), false);
    }
    assert.equal(writes, 0);
    assert.equal(closes, 0);
    assert.equal(connects, 0);
    assert.equal(gateway.active, gateway.webTransport);
});

test('both endpoints reject invalid frames before touching native writers', async () => {
    for (const Endpoint of [WebTransportEndpoint, WebRtcDataChannelEndpoint]) {
        const endpoint = new Endpoint();
        endpoint.state = 'connected';
        let writes = 0;
        endpoint.datagramWriter = endpoint.reliableWriter = {
            ready: Promise.resolve(), write: async () => { ++writes; },
        };
        endpoint.channels = Array.from({length: 3}, () => ({
            readyState: 'open', send: () => { ++writes; },
        }));
        for (const [channel, payload] of invalidFrames) {
            assert.equal(await endpoint.send(channel, payload), false);
        }
        assert.equal(writes, 0);
    }
});

test('a transport write failure still retries once on the fallback', async () => {
    const gateway = new RealtimeGateway();
    gateway.active = gateway.webTransport;
    gateway.url = 'https://example.invalid/session';
    const payload = new Uint8Array([1, 2, 3]);
    let closes = 0;
    let connects = 0;
    gateway.webTransport.send = async () => { throw new Error('transport failed'); };
    gateway.webTransport.close = () => { ++closes; };
    gateway.webRtc.connect = async url => {
        assert.equal(url, gateway.url);
        ++connects;
    };
    gateway.webRtc.send = async (channel, value) => {
        assert.equal(channel, REALTIME);
        assert.deepEqual(value, payload);
        return true;
    };
    assert.equal(await gateway.send(REALTIME, payload), true);
    assert.equal(closes, 1);
    assert.equal(connects, 1);
    assert.equal(gateway.active, gateway.webRtc);
});

test('valid datagrams and reliable frames retain their bytes and framing', async () => {
    const endpoint = new WebTransportEndpoint();
    endpoint.state = 'connected';
    const datagrams = [];
    const reliable = [];
    endpoint.datagramWriter = {
        ready: Promise.resolve(), write: async value => datagrams.push(value),
    };
    endpoint.reliableWriter = {
        ready: Promise.resolve(), write: async value => reliable.push(value),
    };
    const datagram = new Uint8Array(1200).fill(7);
    assert.equal(await endpoint.send(REALTIME, datagram), true);
    assert.deepEqual(datagrams, [datagram]);
    const backing = new Uint8Array([99, 1, 2, 3, 88]);
    const payload = new DataView(backing.buffer, 1, 3);
    for (const channel of [RELIABLE_EVENT, RELIABLE_CONTROL]) {
        assert.equal(await endpoint.send(channel, payload), true);
    }
    assert.equal(reliable.length, 2);
    for (const frame of reliable) {
        assert.deepEqual(frame, new Uint8Array([3, 0, 0, 0, 1, 2, 3]));
    }
});

test('simultaneous failed sends share one fallback connection', async () => {
    const gateway = new RealtimeGateway();
    gateway.active = gateway.webTransport;
    let finishConnecting;
    const connected = new Promise(resolve => { finishConnecting = resolve; });
    let connects = 0;
    let closes = 0;
    const delivered = [];
    gateway.webTransport.send = async () => false;
    gateway.webTransport.close = () => { ++closes; };
    gateway.webRtc.connect = async () => { ++connects; await connected; };
    gateway.webRtc.send = async (channel, payload) => {
        delivered.push([channel, payload[0]]);
        return true;
    };
    const first = gateway.send(REALTIME, new Uint8Array([1]));
    const second = gateway.send(RELIABLE_CONTROL, new Uint8Array([2]));
    await Promise.resolve();
    finishConnecting();
    assert.deepEqual(await Promise.all([first, second]), [true, true]);
    assert.equal(connects, 1);
    assert.equal(closes, 1);
    assert.deepEqual(delivered, [[REALTIME, 1], [RELIABLE_CONTROL, 2]]);
});

function deferred() {
    let resolve;
    let reject;
    const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
    return {promise, resolve, reject};
}

test('an old WebTransport close cannot disconnect its replacement', async t => {
    const connections = [];
    class FakeWebTransport {
        constructor() {
            this.ready = Promise.resolve();
            this.completion = deferred();
            this.closed = this.completion.promise;
            this.datagrams = {
                writable: new WritableStream(),
                readable: new ReadableStream({start: controller => controller.close()}),
            };
            connections.push(this);
        }
        async createBidirectionalStream() {
            return {
                writable: new WritableStream(),
                readable: new ReadableStream({start: controller => controller.close()}),
            };
        }
        close() {}
    }
    const previous = Object.getOwnPropertyDescriptor(globalThis, 'WebTransport');
    globalThis.WebTransport = FakeWebTransport;
    t.after(() => {
        if (previous) Object.defineProperty(globalThis, 'WebTransport', previous);
        else delete globalThis.WebTransport;
    });
    const endpoint = new WebTransportEndpoint();
    await endpoint.connect('https://example.invalid/first');
    endpoint.close();
    await endpoint.connect('https://example.invalid/second');
    connections[0].completion.resolve();
    await Promise.resolve();
    assert.equal(endpoint.state, 'connected');
    endpoint.close();
});

test('closing a gateway while fallback connects keeps it closed', async () => {
    const gateway = new RealtimeGateway();
    gateway.active = gateway.webTransport;
    const connecting = deferred();
    gateway.webTransport.close = () => {};
    gateway.webRtc.close = () => {};
    gateway.webRtc.connect = () => connecting.promise;
    const fallback = gateway.failover();
    gateway.close();
    connecting.resolve();
    assert.equal(await fallback, false);
    assert.equal(gateway.active, null);
});

test('closing WebRTC during signaling cancels the pending connection', async t => {
    const signaling = deferred();
    class FakePeer {
        createDataChannel() {
            return {readyState: 'open', close() { this.readyState = 'closed'; }};
        }
        close() {}
    }
    const previous = Object.getOwnPropertyDescriptor(globalThis, 'RTCPeerConnection');
    globalThis.RTCPeerConnection = FakePeer;
    t.after(() => {
        if (previous) Object.defineProperty(globalThis, 'RTCPeerConnection', previous);
        else delete globalThis.RTCPeerConnection;
    });
    const endpoint = new WebRtcDataChannelEndpoint({
        signaling: {connect: () => signaling.promise},
    });
    const connecting = endpoint.connect('https://example.invalid/session');
    endpoint.close();
    signaling.resolve();
    let timeout;
    const outcome = await Promise.race([
        connecting.then(() => 'connected', () => 'cancelled'),
        new Promise(resolve => { timeout = setTimeout(() => resolve('hung'), 20); }),
    ]);
    clearTimeout(timeout);
    assert.equal(outcome, 'cancelled');
    assert.equal(endpoint.state, 'disconnected');
});

test('a queued write cannot move to a replacement WebTransport writer', async () => {
    for (const channel of [REALTIME, RELIABLE_CONTROL]) {
        const endpoint = new WebTransportEndpoint();
        endpoint.state = 'connected';
        const ready = deferred();
        let released = false;
        const original = {
            ready: ready.promise,
            releaseLock() { released = true; },
            async write() {
                if (released) throw new Error('Writer released');
            },
        };
        const field = channel === REALTIME ? 'datagramWriter' : 'reliableWriter';
        endpoint[field] = original;
        const sending = endpoint.send(channel, new Uint8Array([1]));
        endpoint.close();
        let replacementWrites = 0;
        endpoint[field] = {write: async () => { ++replacementWrites; }};
        ready.resolve();
        await assert.rejects(sending, /Writer released/);
        assert.equal(replacementWrites, 0);
    }
});

test('a cancelled send cannot retry on a newly connected session', async () => {
    const gateway = new RealtimeGateway();
    const sending = deferred();
    gateway.active = gateway.webTransport;
    gateway.webTransport.send = () => sending.promise;
    const result = gateway.send(REALTIME, new Uint8Array([1]));
    gateway.close();
    gateway.active = gateway.webTransport;
    let failovers = 0;
    gateway.failover = async () => { ++failovers; return false; };
    sending.resolve(false);
    assert.equal(await result, false);
    assert.equal(failovers, 0);
});
