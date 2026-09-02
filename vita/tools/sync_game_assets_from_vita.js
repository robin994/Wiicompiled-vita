#!/usr/bin/env node
'use strict';

// Pull only the two user-owned compile inputs from an already extracted MKW
// DATA tree staged on the Vita. The full disc tree stays on ux0 for runtime
// file I/O and is never copied into a VPK or committed to the repository.

const crypto = require('crypto');
const fs = require('fs');
const net = require('net');
const path = require('path');

const HOST = process.env.VITA_FTP_HOST || '192.168.1.217';
const PORT = Number(process.env.VITA_FTP_PORT || '1337');
const REMOTE_ROOT = process.env.MKW_VITA_DATA_ROOT || 'ux0:/data/wiicompiled-vita/game/DATA';

const files = [
  {
    remote: `${REMOTE_ROOT}/sys/main.dol`,
    local: 'Assets/main.dol',
    sha256: '80d18895b39c63bd80f457398bfcbb91b7d16ac116a41a88967e954080155b05',
  },
  {
    remote: `${REMOTE_ROOT}/files/rel/StaticR.rel`,
    local: 'Assets/StaticR.rel',
    sha256: '16d9d146112541fefea701ecb5bc1a496f9d50e4a752fbb5b6778e7c6399f67d',
  },
];

class FtpClient {
  constructor() {
    this.socket = null;
    this.buffer = '';
    this.pending = [];
  }

  waitReply(timeoutMs = 8000) {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('FTP reply timeout')), timeoutMs);
      this.pending.push(reply => {
        clearTimeout(timer);
        resolve(reply);
      });
    });
  }

  onData(data) {
    this.buffer += data;
    for (;;) {
      const match = this.buffer.match(/^(\d{3})([ -])([^\r\n]*)\r?\n/);
      if (!match) return;
      const [whole, codeText, sep, first] = match;
      const code = Number(codeText);
      if (sep === ' ') {
        this.buffer = this.buffer.slice(whole.length);
        const waiter = this.pending.shift();
        if (waiter) waiter({ code, text: `${codeText} ${first}` });
        continue;
      }
      const endMarker = `\n${codeText} `;
      const end = this.buffer.indexOf(endMarker, whole.length - 1);
      if (end < 0) return;
      const lineEnd = this.buffer.indexOf('\n', end + 1);
      if (lineEnd < 0) return;
      const text = this.buffer.slice(0, lineEnd + 1).trim();
      this.buffer = this.buffer.slice(lineEnd + 1);
      const waiter = this.pending.shift();
      if (waiter) waiter({ code, text });
    }
  }

  async connect() {
    this.socket = net.createConnection({ host: HOST, port: PORT });
    this.socket.setEncoding('utf8');
    this.socket.on('data', data => this.onData(data));
    await new Promise((resolve, reject) => {
      this.socket.once('connect', resolve);
      this.socket.once('error', reject);
    });
    const greeting = await this.waitReply();
    if (greeting.code !== 220) throw new Error(`Unexpected FTP greeting: ${greeting.text}`);
  }

  async command(command, allowed) {
    this.socket.write(`${command}\r\n`);
    const reply = await this.waitReply();
    if (!allowed.includes(reply.code)) throw new Error(`${command}: ${reply.text}`);
    return reply;
  }

  async login() {
    const user = await this.command('USER anonymous', [230, 331]);
    if (user.code === 331) await this.command('PASS anonymous', [230]);
    await this.command('TYPE I', [200]);
  }

  async passiveEndpoint() {
    const reply = await this.command('PASV', [227]);
    const match = reply.text.match(/\((\d+),(\d+),(\d+),(\d+),(\d+),(\d+)\)/);
    if (!match) throw new Error(`Cannot parse PASV reply: ${reply.text}`);
    return {
      host: `${match[1]}.${match[2]}.${match[3]}.${match[4]}`,
      port: Number(match[5]) * 256 + Number(match[6]),
    };
  }

  async download(remotePath, localPath) {
    const endpoint = await this.passiveEndpoint();
    const chunks = [];
    const dataSocket = net.createConnection(endpoint);
    await new Promise((resolve, reject) => {
      dataSocket.once('connect', resolve);
      dataSocket.once('error', reject);
    });
    dataSocket.on('data', chunk => chunks.push(Buffer.from(chunk)));

    this.socket.write(`RETR ${remotePath}\r\n`);
    const preliminary = await this.waitReply();
    if (preliminary.code !== 125 && preliminary.code !== 150) {
      dataSocket.destroy();
      throw new Error(`RETR ${remotePath}: ${preliminary.text}`);
    }

    const completion = this.waitReply(30000);
    await new Promise((resolve, reject) => {
      dataSocket.once('end', resolve);
      dataSocket.once('close', resolve);
      dataSocket.once('error', reject);
    });
    const complete = await completion;
    if (complete.code !== 226 && complete.code !== 250) {
      throw new Error(`RETR completion ${remotePath}: ${complete.text}`);
    }

    const data = Buffer.concat(chunks);
    fs.mkdirSync(path.dirname(localPath), { recursive: true });
    fs.writeFileSync(localPath, data);
    return data;
  }

  async close() {
    try { await this.command('QUIT', [221]); } catch (_) {}
    this.socket.end();
  }
}

function sha256(data) {
  return crypto.createHash('sha256').update(data).digest('hex');
}

(async () => {
  const ftp = new FtpClient();
  await ftp.connect();
  await ftp.login();
  try {
    for (const file of files) {
      const data = await ftp.download(file.remote, file.local);
      const digest = sha256(data);
      if (digest !== file.sha256) {
        fs.rmSync(file.local, { force: true });
        throw new Error(
          `${file.remote} is not the expected PAL RMCP01 revision\n` +
          `expected ${file.sha256}\nactual   ${digest}`);
      }
      console.log(`verified ${file.remote} -> ${file.local} (${data.length} bytes, ${digest})`);
    }
  } finally {
    await ftp.close();
  }
})().catch(error => {
  console.error(error.stack || String(error));
  process.exitCode = 1;
});
