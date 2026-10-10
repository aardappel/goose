'use strict';

const { spawn } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const { absolute, settings } = require('./config');

// Only a checkout of the repository has the sources. An installed extension
// does not carry them, because they need a compiler of the same version.
function serverSource(extensionDirectory = path.resolve(__dirname, '..')) {
    const checkout = path.resolve(extensionDirectory, '../tools/gls/main.goose');
    return fs.existsSync(checkout) ? checkout : null;
}

function onPath(name, env = process.env) {
    return (env.PATH || '').split(path.delimiter).filter(Boolean)
        .map(dir => path.join(dir, name)).find(file => fs.existsSync(file) && fs.statSync(file).isFile()) || null;
}

// The language server command and arguments, or null if there is none. In order:
// the configured executable, goose-tools built in the workspace or found on
// PATH, and, in a checkout of the repository, the sources run by the compiler.
function serverCommand(config, configured, fsPath, env = process.env) {
    if (configured) return { command: absolute(configured, config.cwd, fsPath), args: ['--lsp'] };
    const name = process.platform === 'win32' ? 'goose-tools.exe' : 'goose-tools';
    const built = ['', 'Release', 'Debug'].map(dir => path.join(config.cwd, 'build', dir, name)).find(file => fs.existsSync(file));
    const found = built || onPath(name, env);
    if (found) return { command: found, args: ['--lsp'] };
    const source = serverSource();
    if (!source) return null;
    return { command: config.compiler, args: [...(config.stdlib ? ['--stdlib', config.stdlib] : []), '--jit', source, '--', '--lsp'] };
}

class LspConnection {
    constructor(command, args, cwd, log = () => { }, onNotification = () => { }, initializationOptions = {}) {
        this.pending = new Map();
        this.documents = new Map();
        this.sequence = 0;
        this.buffer = Buffer.alloc(0);
        this.dead = false;
        this.onNotification = onNotification;
        this.child = spawn(command, args, { cwd, windowsHide: true, shell: false });
        this.child.stderr.on('data', data => log(data.toString('utf8').trimEnd()));
        this.child.stdout.on('data', data => {
            try { this.receive(data); } catch (error) { this.fail(error); this.child.kill(); }
        });
        this.child.stdin.on('error', error => this.fail(error));
        this.child.on('error', error => this.fail(error));
        this.child.on('exit', (code, signal) => this.fail(new Error(`Goose language server exited (${signal || code}).`)));
        this.ready = this.request('initialize', {
            processId: process.pid, rootUri: null, initializationOptions, capabilities: {
                general: { positionEncodings: ['utf-16'] },
                textDocument: { synchronization: { willSaveWaitUntil: true }, hover: { contentFormat: ['markdown', 'plaintext'] } }
            }
        }).then(() => this.notify('initialized', {}));
        // Construction can precede the caller awaiting readiness.
        this.ready.catch(() => { });
    }

    receive(chunk) {
        this.buffer = Buffer.concat([this.buffer, chunk]);
        while (true) {
            const end = this.buffer.indexOf('\r\n\r\n');
            if (end < 0) {
                if (this.buffer.length > 8192) throw new Error('Oversized LSP header.');
                return;
            }
            const header = this.buffer.subarray(0, end).toString('ascii');
            const match = /^Content-Length:\s*(\d+)\s*$/im.exec(header);
            const length = match && Number(match[1]);
            if (!match || length > 16 * 1024 * 1024) throw new Error('Invalid LSP Content-Length.');
            if (this.buffer.length < end + 4 + length) return;
            const message = JSON.parse(this.buffer.subarray(end + 4, end + 4 + length).toString('utf8'));
            this.buffer = this.buffer.subarray(end + 4 + length);
            if (message.method && message.id === undefined) {
                this.onNotification(message.method, message.params);
                continue;
            }
            const pending = this.pending.get(message.id);
            if (!pending) continue;
            this.pending.delete(message.id);
            clearTimeout(pending.timer);
            if (message.error) pending.reject(new Error(message.error.message));
            else pending.resolve(message.result);
        }
    }

    send(message) {
        if (this.dead) throw new Error('Goose language server is not running.');
        const body = Buffer.from(JSON.stringify({ jsonrpc: '2.0', ...message }), 'utf8');
        this.child.stdin.write(Buffer.concat([Buffer.from(`Content-Length: ${body.length}\r\n\r\n`), body]));
    }

    notify(method, params) { this.send({ method, params }); }

    request(method, params) {
        const id = ++this.sequence;
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => {
                this.pending.delete(id);
                reject(new Error(`Goose LSP request timed out: ${method}`));
                this.fail(new Error('Goose LSP timed out.'));
                this.child.kill();
            }, 30000);
            this.pending.set(id, { resolve, reject, timer });
            try { this.send({ id, method, params }); }
            catch (error) { this.pending.delete(id); clearTimeout(timer); reject(error); }
        });
    }

    sync(document) {
        const uri = document.uri.toString();
        const text = document.getText();
        const old = this.documents.get(uri);
        if (!old) {
            this.notify('textDocument/didOpen', { textDocument: { uri, languageId: 'goose', version: document.version, text } });
        } else if (old.version !== document.version) {
            this.notify('textDocument/didChange', { textDocument: { uri, version: document.version }, contentChanges: [{ text }] });
        }
        this.documents.set(uri, { version: document.version });
    }

    close(uri) {
        if (!this.dead && this.documents.delete(uri)) this.notify('textDocument/didClose', { textDocument: { uri } });
    }

    fail(error) {
        this.dead = true;
        for (const item of this.pending.values()) { clearTimeout(item.timer); item.reject(error); }
        this.pending.clear();
    }

    dispose() {
        if (this.dead) return;
        // Shutdown has a time limit, even if the child stops answering.
        void this.request('shutdown', null).then(() => this.notify('exit', null)).catch(() => { }).finally(() => {
            this.fail(new Error('Goose language server stopped.'));
            this.child.kill();
        });
    }
}

function registerLsp(vscode, context, output) {
    const clients = new Map();
    const diagnostics = vscode.languages.createDiagnosticCollection('goose-syntax');
    const selector = [{ language: 'goose', scheme: 'file' }, { language: 'goose', scheme: 'untitled' }];
    let disposed = false;
    let reportedMissing = false;
    async function call(document, method, extra, token) {
        if (disposed || !vscode.workspace.isTrusted || token?.isCancellationRequested) return null;
        const config = settings(vscode, document.uri);
        let client = clients.get(config.cwd);
        if (!client || client.dead) {
            const serverPath = vscode.workspace.getConfiguration('goose', document.uri).get('languageServerPath', '');
            const server = serverCommand(config, serverPath, document.uri.fsPath);
            if (!server) {
                if (!reportedMissing) {
                    reportedMissing = true;
                    const message = 'Goose language server (goose-tools) not found, so formatting, hover and completion are off. Build it with "cmake --build build --target goose-tools", put it on PATH, or set goose.languageServerPath.';
                    output.appendLine(message);
                    void vscode.window.showWarningMessage(message);
                }
                return null;
            }
            client = new LspConnection(server.command, server.args, config.cwd, message => output.appendLine(message), (method, params) => {
                if (disposed || method !== 'textDocument/publishDiagnostics') return;
                const open = vscode.workspace.textDocuments.find(doc => doc.uri.toString() === params.uri);
                if (!open || open.version !== params.version) return;
                diagnostics.set(open.uri, params.diagnostics.map(item => {
                    const diagnostic = new vscode.Diagnostic(range(item.range), item.message, item.severity === 2 ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Error);
                    diagnostic.source = item.source;
                    return diagnostic;
                }));
            }, { semanticDiagnostics: false }); // The extension runs the compiler itself, with its own settings.
            clients.set(config.cwd, client);
        }
        try {
            await client.ready;
            if (disposed || token?.isCancellationRequested) return null;
            client.sync(document);
            const version = document.version;
            const result = await client.request(method, { textDocument: { uri: document.uri.toString() }, ...extra });
            return document.version === version && !token?.isCancellationRequested ? result : null;
        } catch (error) {
            output.appendLine(`Language server: ${error.message}`);
            if (method === 'textDocument/formatting') throw error;
            return null;
        }
    }
    const position = p => ({ line: p.line, character: p.character });
    const range = r => new vscode.Range(r.start.line, r.start.character, r.end.line, r.end.character);
    const registrations = [
        diagnostics,
        vscode.languages.registerDocumentFormattingEditProvider(selector, {
            async provideDocumentFormattingEdits(document, options, token) {
                const edits = await call(document, 'textDocument/formatting', { options }, token);
                return (edits || []).map(edit => vscode.TextEdit.replace(range(edit.range), edit.newText));
            }
        }),
        vscode.languages.registerCompletionItemProvider(selector, {
            async provideCompletionItems(document, pos, token) {
                const result = await call(document, 'textDocument/completion', { position: position(pos) }, token);
                return (result?.items || []).map(item => new vscode.CompletionItem(item.label,
                    item.kind === 14 ? vscode.CompletionItemKind.Keyword : item.kind === 3 ? vscode.CompletionItemKind.Function : vscode.CompletionItemKind.Variable));
            }
        }),
        vscode.languages.registerHoverProvider(selector, {
            async provideHover(document, pos, token) {
                const result = await call(document, 'textDocument/hover', { position: position(pos) }, token);
                if (!result) return null;
                const content = new vscode.MarkdownString();
                if (result.contents.kind === 'markdown') content.appendMarkdown(result.contents.value);
                else content.appendText(result.contents.value);
                content.isTrusted = false;
                content.supportHtml = false;
                return new vscode.Hover(content, range(result.range));
            }
        }),
        vscode.languages.registerDefinitionProvider(selector, {
            async provideDefinition(document, pos, token) {
                const result = await call(document, 'textDocument/definition', { position: position(pos) }, token);
                return result ? new vscode.Location(vscode.Uri.parse(result.uri), range(result.range)) : null;
            }
        }),
        vscode.workspace.onDidCloseTextDocument(document => {
            for (const client of clients.values()) client.close(document.uri.toString());
            diagnostics.delete(document.uri);
        }),
        vscode.workspace.onDidChangeTextDocument(event => {
            if (event.document.languageId !== 'goose' || !event.contentChanges.length) return;
            diagnostics.delete(event.document.uri);
            const client = clients.get(settings(vscode, event.document.uri).cwd);
            if (client && !client.dead) void client.ready.then(() => {
                if (!disposed && !client.dead && !event.document.isClosed) client.sync(event.document);
            }).catch(() => { });
        }),
        vscode.workspace.onDidChangeConfiguration(event => {
            if (!event.affectsConfiguration('goose')) return;
            for (const client of clients.values()) client.dispose();
            clients.clear();
            diagnostics.clear();
            reportedMissing = false;
        }),
        { dispose() { disposed = true; for (const client of clients.values()) client.dispose(); clients.clear(); } }
    ];
    context.subscriptions.push(...registrations);
}

module.exports = { LspConnection, registerLsp, serverSource, serverCommand };
