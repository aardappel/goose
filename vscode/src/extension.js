'use strict';

const vscode = require('vscode');
const { settings, argumentsFor, stdlibDirectories, absolute } = require('./config');
const { parseDiagnostics, runCompiler, CheckQueue } = require('./diagnostics');
const { declarations, imports, importCandidates } = require('./language');

const isGoose = doc => doc.languageId === 'goose' && doc.uri.scheme === 'file';

function activate(context) {
    const diagnostics = vscode.languages.createDiagnosticCollection('goose');
    const output = vscode.window.createOutputChannel('Goose');
    let disposed = false;
    let publication = 0;
    let reportedFailure = '';
    const loggedResults = new WeakSet();

    async function publish(results) {
        if (disposed) return;
        const generation = ++publication;
        diagnostics.clear();
        const grouped = new Map();
        const lines = new Map();
        async function sourceLine(file, line) {
            if (!lines.has(file)) {
                const open = vscode.workspace.textDocuments.find(doc => doc.uri.fsPath === file);
                try {
                    const source = open ? open.getText() : Buffer.from(await vscode.workspace.fs.readFile(vscode.Uri.file(file))).toString('utf8');
                    lines.set(file, source.split(/\r?\n/));
                } catch { lines.set(file, []); }
            }
            return lines.get(file)[line] || '';
        }
        for (const result of results.values()) {
            const { config, error, output: text } = result;
            const parsed = parseDiagnostics(text, config.cwd);
            if (!loggedResults.has(result)) {
                loggedResults.add(result);
                output.appendLine(`> ${config.compiler} ${argumentsFor('check', config).map(arg => JSON.stringify(arg)).join(' ')}`);
                if (text.trim()) output.appendLine(text.trimEnd());
                if (error && !parsed.some(item => item.severity === 'error')) output.appendLine(error.message);
            }
            if (error && !parsed.some(item => item.severity === 'error')) {
                // No location (e.g. missing import or missing executable): still make
                // the failure visible, but never invent a position in an import.
                const message = error.code === 'ENOENT'
                    ? `Goose compiler not found: ${config.compiler}. Set goose.compilerPath to the executable.`
                    : error.killed ? `Goose check exceeded ${config.timeout} ms or was terminated.`
                    : text.trim() || error.message;
                parsed.push({ file: config.file, line: 0, severity: 'error', message, related: [] });
                if (error.code === 'ENOENT' && reportedFailure !== message) {
                    reportedFailure = message;
                    void vscode.window.showWarningMessage(message);
                }
            } else if (!error) reportedFailure = '';
            for (const item of parsed) {
                const line = await sourceLine(item.file, item.line);
                const start = item.column === undefined ? Math.max(0, line.search(/\S/)) : Math.min(item.column, line.length);
                const end = item.column === undefined ? line.length : Math.min(line.length, start + (line.slice(start).match(/^[A-Za-z_][A-Za-z0-9_]*/)?.[0].length || 1));
                const range = new vscode.Range(item.line, start, item.line, Math.max(start, end));
                const diagnostic = new vscode.Diagnostic(range, item.message,
                    item.severity === 'warning' ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Error);
                diagnostic.source = 'Goose';
                diagnostic.relatedInformation = item.related.map(frame => new vscode.DiagnosticRelatedInformation(
                    new vscode.Location(vscode.Uri.file(frame.file), new vscode.Range(frame.line, 0, frame.line, 0)), frame.message));
                const entry = grouped.get(item.file) || [];
                if (!entry.some(other => other.message === diagnostic.message && other.range.isEqual(range))) entry.push(diagnostic);
                grouped.set(item.file, entry);
            }
        }
        if (!disposed && generation === publication) {
            diagnostics.set([...grouped].map(([file, items]) => [vscode.Uri.file(file), items]));
        }
    }

    const queue = new CheckQueue(runCompiler, results => {
        void publish(results).catch(error => output.appendLine(`Diagnostics: ${error.message}`));
    });
    const cwdOf = doc => settings(vscode, doc.uri).cwd;
    const dirtyIn = cwd => vscode.workspace.textDocuments.some(doc => isGoose(doc) && doc.isDirty && cwdOf(doc) === cwd);

    function check(doc) {
        if (disposed || !vscode.workspace.isTrusted || !isGoose(doc) || doc.isDirty) return;
        const config = settings(vscode, doc.uri);
        if (config.checkOnSave && !dirtyIn(config.cwd)) void queue.schedule(config);
    }

    function recheck(cwd) {
        const checked = new Set();
        for (const doc of vscode.workspace.textDocuments) {
            if (!isGoose(doc)) continue;
            const config = settings(vscode, doc.uri);
            if ((!cwd || config.cwd === cwd) && !checked.has(config.file) && config.checkOnSave) {
                checked.add(config.file);
                check(doc);
            }
        }
    }

    async function savedActiveDocument() {
        const doc = vscode.window.activeTextEditor?.document;
        if (!doc || doc.languageId !== 'goose') return undefined;
        if (!vscode.workspace.isTrusted) {
            void vscode.window.showWarningMessage('Trust this workspace to run the Goose compiler.');
            return undefined;
        }
        if (doc.isUntitled && !await doc.save()) return undefined;
        const active = vscode.window.activeTextEditor?.document;
        if (!active || !isGoose(active)) return undefined;
        const cwd = cwdOf(active);
        for (const open of vscode.workspace.textDocuments) {
            if (isGoose(open) && open.isDirty && cwdOf(open) === cwd && !await open.save()) return undefined;
        }
        return active;
    }

    function makeTask(definition, uri, scope, name) {
        if (!vscode.workspace.isTrusted || uri.scheme !== 'file') return undefined;
        const config = settings(vscode, uri, definition, typeof scope === 'object' ? scope : undefined);
        const titles = { check: 'Check Program', run: 'Run Program', generateC: 'Generate C' };
        if (!titles[definition.action]) return undefined;
        const task = new vscode.Task({ ...definition, type: 'goose', file: config.file },
            scope || config.workspaceFolder || vscode.TaskScope.Workspace,
            name || titles[definition.action], 'Goose',
            new vscode.ProcessExecution(config.compiler, argumentsFor(definition.action, config), { cwd: config.cwd }), '$goose');
        if (definition.action === 'check' || definition.action === 'generateC') task.group = vscode.TaskGroup.Build;
        task.presentationOptions = { reveal: vscode.TaskRevealKind.Always, panel: vscode.TaskPanelKind.Dedicated, clear: true };
        return task;
    }

    context.subscriptions.push(
        diagnostics, output,
        { dispose() { disposed = true; queue.invalidate(); } },
        vscode.commands.registerCommand('goose.showOutput', () => output.show()),
        vscode.commands.registerCommand('goose.check', async () => {
            const doc = await savedActiveDocument();
            if (!doc) return;
            const completed = await queue.schedule(settings(vscode, doc.uri), 0);
            if (completed) output.show(true);
        }),
        ...['run', 'generateC'].map(action => vscode.commands.registerCommand(`goose.${action}`, async () => {
            const doc = await savedActiveDocument();
            if (doc) await vscode.tasks.executeTask(makeTask({ type: 'goose', action }, doc.uri));
        })),
        vscode.tasks.registerTaskProvider('goose', {
            provideTasks() {
                const doc = vscode.window.activeTextEditor?.document;
                if (!doc || !isGoose(doc) || !vscode.workspace.isTrusted) return [];
                return ['check', 'run', 'generateC'].map(action => makeTask({ type: 'goose', action }, doc.uri));
            },
            resolveTask(task) {
                if (!vscode.workspace.isTrusted) return undefined;
                const folder = typeof task.scope === 'object' ? task.scope : undefined;
                const active = vscode.window.activeTextEditor?.document;
                const base = folder?.uri.fsPath;
                const file = task.definition.file;
                const configuredEntry = folder && vscode.workspace.getConfiguration('goose', folder.uri).get('entryFile', '');
                const uri = file && base && !file.includes('${file}')
                    ? vscode.Uri.file(absolute(file, base, ''))
                    : active && isGoose(active) && (!base || cwdOf(active) === base) ? active.uri
                    : configuredEntry ? vscode.Uri.file(absolute(configuredEntry, base, '')) : undefined;
                if (!uri) return undefined;
                const resolved = makeTask(task.definition, uri, task.scope, task.name);
                if (resolved) {
                    resolved.definition = task.definition;
                    resolved.presentationOptions = task.presentationOptions;
                    resolved.group = task.group;
                    resolved.problemMatchers = task.problemMatchers.length ? task.problemMatchers : ['$goose'];
                    resolved.runOptions = task.runOptions;
                }
                return resolved;
            }
        }),
        vscode.languages.registerDocumentSymbolProvider([{ language: 'goose', scheme: 'file' }, { language: 'goose', scheme: 'untitled' }], {
            provideDocumentSymbols(doc) {
                return declarations(doc.getText()).map(item => new vscode.DocumentSymbol(item.name, item.detail,
                    vscode.SymbolKind[item.kind], new vscode.Range(doc.positionAt(item.rangeStart), doc.positionAt(item.rangeEnd)),
                    new vscode.Range(doc.positionAt(item.start), doc.positionAt(item.end))));
            }
        }),
        vscode.languages.registerDocumentLinkProvider({ language: 'goose', scheme: 'file' }, {
            async provideDocumentLinks(doc, token) {
                const config = settings(vscode, doc.uri);
                const dirs = stdlibDirectories(config);
                const links = [];
                for (const item of imports(doc.getText())) {
                    for (const candidate of importCandidates(item, doc.uri.fsPath, config.file, dirs)) {
                        if (token.isCancellationRequested) return [];
                        try {
                            const uri = vscode.Uri.file(candidate);
                            const stat = await vscode.workspace.fs.stat(uri);
                            if (!(stat.type & vscode.FileType.File)) continue;
                            const link = new vscode.DocumentLink(new vscode.Range(doc.positionAt(item.start), doc.positionAt(item.end)), uri);
                            link.tooltip = 'Open Goose module';
                            links.push(link);
                            break;
                        } catch { /* Try the next location, as the compiler does. */ }
                    }
                }
                return links;
            }
        }),
        vscode.workspace.onDidOpenTextDocument(check),
        vscode.workspace.onDidSaveTextDocument(doc => {
            if (isGoose(doc)) { queue.invalidate(cwdOf(doc)); recheck(cwdOf(doc)); }
        }),
        vscode.workspace.onDidChangeTextDocument(event => {
            if (isGoose(event.document) && event.contentChanges.length) queue.invalidate(cwdOf(event.document));
        }),
        vscode.workspace.onDidCloseTextDocument(doc => {
            if (isGoose(doc)) { queue.invalidate(cwdOf(doc)); recheck(cwdOf(doc)); }
        }),
        vscode.workspace.onDidChangeConfiguration(event => {
            if (event.affectsConfiguration('goose')) { queue.invalidate(); recheck(); }
        }),
        vscode.workspace.onDidChangeWorkspaceFolders(() => { queue.invalidate(); recheck(); }),
        vscode.workspace.onDidGrantWorkspaceTrust(() => recheck())
    );
    const watcher = vscode.workspace.createFileSystemWatcher('**/*.goose');
    const changedOnDisk = async uri => {
        const cwd = settings(vscode, uri).cwd;
        // Filesystem notifications can arrive after a manual check of a just-
        // saved file. Do not cancel that check or erase its current results.
        let modified = Infinity;
        try { modified = (await vscode.workspace.fs.stat(uri)).mtime; } catch { /* Deleted file. */ }
        if (disposed) return;
        const existing = [...queue.results.values(), ...queue.jobs.values()].filter(item => item.config.cwd === cwd);
        if (existing.length && existing.every(item => item.startedAt === undefined || item.startedAt > modified)) return;
        queue.invalidate(cwd);
        recheck(cwd);
    };
    context.subscriptions.push(watcher, watcher.onDidCreate(changedOnDisk), watcher.onDidChange(changedOnDisk), watcher.onDidDelete(changedOnDisk));
    recheck();
}

module.exports = { activate };
