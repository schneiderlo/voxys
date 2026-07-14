(function installVoxyNetworkTransport(global) {
    "use strict";

    const REALTIME = 0;
    const RELIABLE_EVENT = 1;
    const RELIABLE_CONTROL = 2;
    const REALTIME_MTU = 1200;

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
                    if (length > 16 * 1024 * 1024) {
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
            this.state = "connecting";
            this.onState(this.state, "webtransport");
            this.transport = new global.WebTransport(url);
            await this.transport.ready;

            const datagrams = this.transport.datagrams;
            const writable = typeof datagrams.createWritable === "function"
                ? datagrams.createWritable({sendOrder: 0})
                : datagrams.writable;
            this.datagramWriter = writable.getWriter();
            const reliable = await this.transport.createBidirectionalStream();
            this.reliableWriter = reliable.writable.getWriter();
            void readFramed(reliable.readable, this.onFrame, RELIABLE_CONTROL);
            void this.readDatagrams(datagrams.readable);
            void this.readIncomingStreams();
            void this.transport.closed.then(
                () => this.setClosed(),
                () => this.setFailed()
            );
            this.state = "connected";
            this.onState(this.state, "webtransport");
            return true;
        }

        async readDatagrams(readable) {
            const reader = readable.getReader();
            try {
                for (;;) {
                    const {value, done} = await reader.read();
                    if (done) return;
                    this.onFrame(REALTIME, bytes(value));
                }
            } finally {
                reader.releaseLock();
            }
        }

        async readIncomingStreams() {
            if (!this.transport.incomingUnidirectionalStreams) return;
            const reader = this.transport.incomingUnidirectionalStreams.getReader();
            try {
                for (;;) {
                    const {value, done} = await reader.read();
                    if (done) return;
                    void readFramed(value, this.onFrame, RELIABLE_EVENT);
                }
            } finally {
                reader.releaseLock();
            }
        }

        async send(channel, value) {
            if (this.state !== "connected") return false;
            const payload = bytes(value);
            if (channel === REALTIME) {
                if (payload.byteLength > REALTIME_MTU) return false;
                await this.datagramWriter.ready;
                await this.datagramWriter.write(payload);
                return true;
            }
            await this.reliableWriter.ready;
            await this.reliableWriter.write(framed(payload));
            return true;
        }

        close() {
            if (this.datagramWriter) this.datagramWriter.releaseLock();
            if (this.reliableWriter) this.reliableWriter.releaseLock();
            if (this.transport) this.transport.close({closeCode: 0});
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
            this.state = "connecting";
            this.onState(this.state, "webrtc");
            this.peer = new global.RTCPeerConnection(this.rtcConfig);
            this.channels = [
                this.peer.createDataChannel("voxy-realtime", {
                    ordered: false,
                    maxRetransmits: 0
                }),
                this.peer.createDataChannel("voxy-events", {ordered: false}),
                this.peer.createDataChannel("voxy-control", {ordered: true})
            ];
            for (let channel = 0; channel < this.channels.length; ++channel) {
                const dataChannel = this.channels[channel];
                dataChannel.binaryType = "arraybuffer";
                dataChannel.onmessage = event => this.onFrame(
                    channel, bytes(event.data)
                );
            }
            await this.signaling.connect(this.peer, url);
            await Promise.all(this.channels.map(dataChannel => new Promise(
                (resolve, reject) => {
                    if (dataChannel.readyState === "open") return resolve();
                    dataChannel.onopen = resolve;
                    dataChannel.onerror = reject;
                }
            )));
            this.state = "connected";
            this.onState(this.state, "webrtc");
            return true;
        }

        async send(channel, value) {
            const dataChannel = this.channels[channel];
            const payload = bytes(value);
            if (this.state !== "connected" || !dataChannel
                || dataChannel.readyState !== "open") return false;
            if (channel === REALTIME && payload.byteLength > REALTIME_MTU) {
                return false;
            }
            dataChannel.send(payload);
            return true;
        }

        close() {
            for (const channel of this.channels) channel.close();
            if (this.peer) this.peer.close();
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
        }

        async connect(url) {
            this.url = url;
            if (this.webTransport.available()) {
                try {
                    await this.webTransport.connect(url);
                    this.active = this.webTransport;
                    return "webtransport";
                } catch (_) {
                    this.webTransport.close();
                }
            }
            await this.webRtc.connect(url);
            this.active = this.webRtc;
            return "webrtc";
        }

        async failover() {
            if (this.active !== this.webTransport) return false;
            this.webTransport.close();
            await this.webRtc.connect(this.url);
            this.active = this.webRtc;
            return true;
        }

        async send(channel, value) {
            if (!this.active) return false;
            try {
                if (await this.active.send(channel, value)) return true;
            } catch (_) {
                // A failed primary send gets one fallback attempt.
            }
            if (await this.failover()) return this.active.send(channel, value);
            return false;
        }

        close() {
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
