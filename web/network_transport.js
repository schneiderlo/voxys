(function installVoxyNetworkTransport(global) {
    "use strict";

    const REALTIME = 0;
    const RELIABLE_EVENT = 1;
    const RELIABLE_CONTROL = 2;
    const REALTIME_MTU = 1200;
    const MAXIMUM_RELIABLE_FRAME_BYTES = 16 * 1024 * 1024;

    function validFrame(channel, payload) {
        if (payload.byteLength === 0) return false;
        if (channel === REALTIME) return payload.byteLength <= REALTIME_MTU;
        return (channel === RELIABLE_EVENT || channel === RELIABLE_CONTROL)
            && payload.byteLength <= MAXIMUM_RELIABLE_FRAME_BYTES;
    }

    function bytes(value) {
        if (value instanceof Uint8Array) return value;
        if (value instanceof ArrayBuffer) return new Uint8Array(value);
        return new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
    }

    function framed(value) {
        const payload = bytes(value);
        const output = new Uint8Array(payload.byteLength + 4);
        new DataView(output.buffer).setUint32(0, payload.byteLength, true);
        output.set(payload, 4);
        return output;
    }

    async function readFramed(readable, onFrame, channel) {
        const reader = readable.getReader();
        let buffered = new Uint8Array(0);
        try {
            for (;;) {
                const {value, done} = await reader.read();
                if (done) return;
                const incoming = bytes(value);
                const joined = new Uint8Array(buffered.byteLength + incoming.byteLength);
                joined.set(buffered);
                joined.set(incoming, buffered.byteLength);
                buffered = joined;
                while (buffered.byteLength >= 4) {
                    const length = new DataView(
                        buffered.buffer, buffered.byteOffset, 4
                    ).getUint32(0, true);
                    if (length > MAXIMUM_RELIABLE_FRAME_BYTES) {
                        throw new Error("Voxys reliable frame exceeds 16 MiB");
                    }
                    if (buffered.byteLength < length + 4) break;
                    onFrame(channel, buffered.slice(4, length + 4));
                    buffered = buffered.slice(length + 4);
                }
            }
        } finally {
            reader.releaseLock();
        }
    }

    class WebTransportEndpoint {
        constructor({onFrame = () => {}, onState = () => {}} = {}) {
            this.onFrame = onFrame;
            this.onState = onState;
            this.transport = null;
            this.datagramWriter = null;
            this.reliableWriter = null;
            this.state = "disconnected";
        }

        available() {
            return typeof global.WebTransport === "function";
        }

        async connect(url) {
            if (!this.available()) throw new Error("WebTransport unavailable");
            if (this.transport) this.close();
            this.state = "connecting";
            this.onState(this.state, "webtransport");
            const transport = new global.WebTransport(url);
            this.transport = transport;
            const fail = () => {
                if (this.transport === transport) this.setFailed();
            };
            void transport.closed.then(
                () => { if (this.transport === transport) this.setClosed(); },
                fail
            );
            await transport.ready;
            if (this.transport !== transport) throw new Error("Connection cancelled");

            const datagrams = transport.datagrams;
            const writable = typeof datagrams.createWritable === "function"
                ? datagrams.createWritable({sendOrder: 0})
                : datagrams.writable;
            this.datagramWriter = writable.getWriter();
            const reliable = await transport.createBidirectionalStream();
            if (this.transport !== transport) throw new Error("Connection cancelled");
            this.reliableWriter = reliable.writable.getWriter();
            const onFrame = (channel, payload) => {
                if (this.transport === transport) this.onFrame(channel, payload);
            };
            void readFramed(reliable.readable, onFrame, RELIABLE_CONTROL).catch(fail);
            void this.readDatagrams(datagrams.readable, onFrame).catch(fail);
            void this.readIncomingStreams(transport, onFrame, fail).catch(fail);
            this.state = "connected";
            this.onState(this.state, "webtransport");
            return true;
        }

        async readDatagrams(readable, onFrame) {
            const reader = readable.getReader();
            try {
                for (;;) {
                    const {value, done} = await reader.read();
                    if (done) return;
                    onFrame(REALTIME, bytes(value));
                }
            } finally {
                reader.releaseLock();
            }
        }

        async readIncomingStreams(transport, onFrame, fail) {
            if (!transport.incomingUnidirectionalStreams) return;
            const reader = transport.incomingUnidirectionalStreams.getReader();
            try {
                for (;;) {
                    const {value, done} = await reader.read();
                    if (done) return;
                    void readFramed(value, onFrame, RELIABLE_EVENT).catch(fail);
                }
            } finally {
                reader.releaseLock();
            }
        }

        async send(channel, value) {
            if (this.state !== "connected") return false;
            const payload = bytes(value);
            if (!validFrame(channel, payload)) return false;
            if (channel === REALTIME) {
                const writer = this.datagramWriter;
                await writer.ready;
                await writer.write(payload);
                return true;
            }
            const writer = this.reliableWriter;
            await writer.ready;
            await writer.write(framed(payload));
            return true;
        }

        close() {
            const transport = this.transport;
            this.transport = null;
            if (this.datagramWriter) this.datagramWriter.releaseLock();
            if (this.reliableWriter) this.reliableWriter.releaseLock();
            this.datagramWriter = null;
            this.reliableWriter = null;
            if (transport) transport.close({closeCode: 0});
            this.setClosed();
        }

        setClosed() {
            this.state = "disconnected";
            this.onState(this.state, "webtransport");
        }

        setFailed() {
            this.state = "failed";
            this.onState(this.state, "webtransport");
        }
    }

    class WebRtcDataChannelEndpoint {
        constructor({signaling, rtcConfig = {}, onFrame = () => {},
                     onState = () => {}} = {}) {
            this.signaling = signaling;
            this.rtcConfig = rtcConfig;
            this.onFrame = onFrame;
            this.onState = onState;
            this.peer = null;
            this.channels = [];
            this.state = "disconnected";
        }

        available() {
            return typeof global.RTCPeerConnection === "function"
                && this.signaling && typeof this.signaling.connect === "function";
        }

        async connect(url) {
            if (!this.available()) throw new Error("WebRTC signaling unavailable");
            if (this.peer) this.close();
            this.state = "connecting";
            this.onState(this.state, "webrtc");
            const peer = new global.RTCPeerConnection(this.rtcConfig);
            this.peer = peer;
            const channels = [
                peer.createDataChannel("voxy-realtime", {
                    ordered: false,
                    maxRetransmits: 0
                }),
                peer.createDataChannel("voxy-events", {ordered: false}),
                peer.createDataChannel("voxy-control", {ordered: true})
            ];
            this.channels = channels;
            for (let channel = 0; channel < channels.length; ++channel) {
                const dataChannel = channels[channel];
                dataChannel.binaryType = "arraybuffer";
                dataChannel.onmessage = event => {
                    if (this.peer === peer) this.onFrame(channel, bytes(event.data));
                };
            }
            await this.signaling.connect(peer, url);
            if (this.peer !== peer) throw new Error("Connection cancelled");
            await Promise.all(channels.map(dataChannel => new Promise(
                (resolve, reject) => {
                    if (dataChannel.readyState === "open") return resolve();
                    if (dataChannel.readyState !== "connecting") {
                        return reject(new Error("Data channel closed"));
                    }
                    dataChannel.onopen = resolve;
                    dataChannel.onerror = reject;
                    dataChannel.onclose = () => reject(new Error("Data channel closed"));
                }
            )));
            if (this.peer !== peer) throw new Error("Connection cancelled");
            this.state = "connected";
            this.onState(this.state, "webrtc");
            return true;
        }

        async send(channel, value) {
            const dataChannel = this.channels[channel];
            const payload = bytes(value);
            if (this.state !== "connected" || !dataChannel
                || dataChannel.readyState !== "open") return false;
            if (!validFrame(channel, payload)) return false;
            dataChannel.send(payload);
            return true;
        }

        close() {
            const peer = this.peer;
            const channels = this.channels;
            this.peer = null;
            this.channels = [];
            for (const channel of channels) channel.close();
            if (peer) peer.close();
            this.state = "disconnected";
            this.onState(this.state, "webrtc");
        }
    }

    class RealtimeGateway {
        constructor(options = {}) {
            this.webTransport = new WebTransportEndpoint(options);
            this.webRtc = new WebRtcDataChannelEndpoint(options);
            this.active = null;
            this.url = "";
            this.pendingFailover = null;
            this.generation = 0;
        }

        async connect(url) {
            this.close();
            const generation = this.generation;
            this.url = url;
            if (this.webTransport.available()) {
                try {
                    await this.webTransport.connect(url);
                    if (generation !== this.generation) throw new Error("Connection cancelled");
                    this.active = this.webTransport;
                    return "webtransport";
                } catch (_) {
                    if (generation !== this.generation) throw new Error("Connection cancelled");
                    this.webTransport.close();
                }
            }
            await this.webRtc.connect(url);
            if (generation !== this.generation) throw new Error("Connection cancelled");
            this.active = this.webRtc;
            return "webrtc";
        }

        async failover() {
            if (this.active !== this.webTransport) return false;
            if (!this.pendingFailover) {
                const generation = this.generation;
                this.pendingFailover = (async () => {
                    this.webTransport.close();
                    try {
                        await this.webRtc.connect(this.url);
                    } catch (error) {
                        if (generation !== this.generation) return false;
                        throw error;
                    }
                    if (generation !== this.generation) return false;
                    this.active = this.webRtc;
                    return true;
                })().finally(() => {
                    if (generation === this.generation) this.pendingFailover = null;
                });
            }
            return this.pendingFailover;
        }

        async send(channel, value) {
            if (!this.active) return false;
            const generation = this.generation;
            const payload = bytes(value);
            // An invalid caller frame is not a failed transport. Preserve the
            // connection and reserve failover for an actual send failure.
            if (!validFrame(channel, payload)) return false;
            try {
                if (await this.active.send(channel, payload)) return true;
            } catch (_) {
                // A failed primary send gets one fallback attempt.
            }
            if (generation !== this.generation) return false;
            if (await this.failover()) {
                if (generation !== this.generation || !this.active) return false;
                return this.active.send(channel, payload);
            }
            return false;
        }

        close() {
            ++this.generation;
            this.pendingFailover = null;
            this.webTransport.close();
            this.webRtc.close();
            this.active = null;
        }
    }

    global.VoxyNetworkTransport = Object.freeze({
        REALTIME,
        RELIABLE_EVENT,
        RELIABLE_CONTROL,
        WebTransportEndpoint,
        WebRtcDataChannelEndpoint,
        RealtimeGateway
    });
})(globalThis);
