'use strict';

const vscode = require('vscode');
const path = require('node:path');
const { settings, argumentsFor, stdlibDirectories, absolute } = require('./config');
const { parseDiagnostics, failureMessage, runCompiler, CheckQueue } = require('./diagnostics');
const { declarations, imports, importCandidates } = require('./language');

const isGoose = doc => doc.languageId === 'goose' && doc.uri.scheme === 'file';

function activate(context) {
    const diagnostics = vscode.languages.createDiagnosticCollection('goose');
    const output = vscode.window.createOutputChannel('Goose');
    let disposed = false;
    let publication = 0;
    let reportedFailure = '';

    // What the last check of each entry file found: a failure with no source
    // location, or the number of errors, and whether sources changed since.
    let checked = new Map();
    // The status of the active file's entry, in the bottom status bar: its last
    // check, marked stale once sources change, with a spinner while a new check
    // runs. Another entry's results never stand in for it.
    const checkStatus = vscode.window.createStatusBarItem('goose.check', vscode.StatusBarAlignment.Left, 4);
    checkStatus.name = 'Goose: Check';
    function updateCheckStatus() {
        const doc = vscode.window.activeTextEditor?.document;
        if (!vscode.workspace.isTrusted || doc?.languageId !== 'goose') { checkStatus.hide(); return; }
        const config = isGoose(doc) ? settings(vscode, doc.uri) : undefined;
        const running = config && queue.jobs.has(config.file);
        const last = config && checked.get(config.file);
        checkStatus.command = 'goose.showOutput';
        checkStatus.backgroundColor = undefined;
        if (running) {
            checkStatus.text = '$(sync~spin) Goose: Checking';
            checkStatus.tooltip = last && !last.stale ? 'Checking again.' : 'Checking the saved files.';
        } else if (!last) {
            checkStatus.text = '$(circle-outline) Goose';
            checkStatus.tooltip = config?.checkOnSave ? 'Not checked yet. Save to check.' : 'Not checked yet. Run Goose: Check Program to check.';
        } else if (last.problem) {
            checkStatus.text = `$(warning) Goose: ${last.problem.title}`;
            checkStatus.tooltip = `${last.problem.message.split('\n').slice(0, 6).join('\n')}\n\nClick to open the Goose output.`;
            checkStatus.backgroundColor = new vscode.ThemeColor('statusBarItem.warningBackground');
        } else if (last.errors > 0) {
            checkStatus.text = `$(error) Goose: ${last.errors} error${last.errors === 1 ? '' : 's'}`;
            checkStatus.tooltip = 'The last Goose check found errors. Click to open Problems.';
            checkStatus.command = 'workbench.actions.view.problems';
        } else {
            checkStatus.text = '$(check) Goose';
            checkStatus.tooltip = 'The last Goose check found no errors. Click to open the Goose output.';
        }
        if (last?.stale && !running) {
            checkStatus.text += ' (stale)';
            checkStatus.tooltip = `Edited since this check; save to check again.\n\n${checkStatus.tooltip}`;
        }
        checkStatus.show();
    }

    // A result's diagnostics, built once when it lands, against the sources it
    // checked: a stale result keeps them as they were while files are edited.
    async function build(result) {
        const { config, error, output: text } = result;
        const parsed = parseDiagnostics(text, config.cwd);
        output.appendLine(`> ${config.compiler} ${argumentsFor('check', config).map(arg => JSON.stringify(arg)).join(' ')}`);
        if (text.trim()) output.appendLine(text.trimEnd());
        // A failure with no source location is not an error on any line. It is
        // listed in Problems against the checked file at an empty range, so it
        // marks no text. It also goes to the status bar and the output channel.
        let problem;
        if (error && !parsed.some(item => item.severity === 'error')) {
            output.appendLine(error.message);
            const missing = error.code === 'ENOENT';
            const message = missing
                ? `Goose compiler not found: ${config.compiler}. Set goose.compilerPath to the executable.`
                : error.killed ? `Goose check exceeded ${config.timeout} ms or was terminated.`
                : failureMessage(text) || error.message;
            problem = { message, title: missing ? 'Compiler Not Found' : error.killed ? 'Check Timed Out' : 'Check Failed' };
            parsed.push({ file: config.file, line: 0, column: 0, empty: true, severity: 'warning', message: `Goose check failed: ${message}`, related: [] });
            if (missing && reportedFailure !== message) {
                reportedFailure = message;
                void vscode.window.showWarningMessage(message);
            }
        } else if (!error) reportedFailure = '';
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
        const files = new Map();
        let errors = 0;
        for (const item of parsed) {
            const line = await sourceLine(item.file, item.line);
            const start = item.column === undefined ? Math.max(0, line.search(/\S/)) : Math.min(item.column, line.length);
            const end = item.empty ? start : item.column === undefined ? line.length : Math.min(line.length, start + (line.slice(start).match(/^[A-Za-z_][A-Za-z0-9_]*/)?.[0].length || 1));
            const range = new vscode.Range(item.line, start, item.line, Math.max(start, end));
            const diagnostic = new vscode.Diagnostic(range, item.message,
                item.severity === 'warning' ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Error);
            diagnostic.source = 'Goose';
            diagnostic.relatedInformation = item.related.map(frame => new vscode.DiagnosticRelatedInformation(
                new vscode.Location(vscode.Uri.file(frame.file), new vscode.Range(frame.line, 0, frame.line, 0)), frame.message));
            const entry = files.get(item.file) || [];
            if (entry.some(other => other.message === diagnostic.message && other.range.isEqual(range))) continue;
            entry.push(diagnostic);
            files.set(item.file, entry);
            if (diagnostic.severity === vscode.DiagnosticSeverity.Error) errors++;
        }
        return { files, problem, errors };
    }
    const built = new WeakMap();

    async function publish(results) {
        if (disposed) return;
        const generation = ++publication;
        const current = [...results.values()];
        for (const result of current) if (!built.has(result)) built.set(result, build(result));
        const found = await Promise.all(current.map(result => built.get(result)));
        if (disposed || generation !== publication) return;
        const grouped = new Map();
        checked = new Map();
        current.forEach((result, i) => {
            const { files, problem, errors } = found[i];
            checked.set(result.config.file, { problem, errors, stale: !!result.stale });
            for (const [file, items] of files) {
                const entry = grouped.get(file) || [];
                for (const diagnostic of items) {
                    if (!entry.some(other => other.message === diagnostic.message && other.range.isEqual(diagnostic.range))) entry.push(diagnostic);
                }
                grouped.set(file, entry);
            }
        });
        diagnostics.clear();
        diagnostics.set([...grouped].map(([file, items]) => [vscode.Uri.file(file), items]));
        updateCheckStatus();
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

    // Check the entry files of open documents in cwd (or everywhere), and drop
    // the results of entries no open document checks any more.
    function recheck(cwd) {
        const entries = new Map();
        for (const doc of vscode.workspace.textDocuments) {
            if (!isGoose(doc)) continue;
            const config = settings(vscode, doc.uri);
            if (config.checkOnSave && !entries.has(config.file)) entries.set(config.file, { doc, cwd: config.cwd });
        }
        queue.retain(file => entries.has(file));
        for (const entry of entries.values()) if (!cwd || entry.cwd === cwd) check(entry.doc);
    }

    // Sources in cwd were saved, closed or changed on disk: what was found
    // stays, stale, until rechecked.
    function refresh(cwd) {
        queue.stale(cwd);
        recheck(cwd);
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
        const titles = { check: 'Check Program', run: 'Run with JIT', build: 'Build Executable', generateC: 'Generate C' };
        if (!titles[definition.action]) return undefined;
        const task = new vscode.Task({ ...definition, type: 'goose', file: config.file },
            scope || config.workspaceFolder || vscode.TaskScope.Workspace,
            name || titles[definition.action], 'Goose',
            new vscode.ProcessExecution(config.compiler, argumentsFor(definition.action, config), { cwd: config.cwd }), '$goose');
        // VS Code reserves isDefault for tasks.json. Contribute JIT as our only
        // build task; explicit Check/Generate C tasks remain in Tasks: Run Task.
        if (definition.action === 'run') task.group = vscode.TaskGroup.Build;
        task.presentationOptions = { reveal: vscode.TaskRevealKind.Always, panel: vscode.TaskPanelKind.Dedicated, clear: true };
        return task;
    }

    const runStatus = vscode.window.createStatusBarItem('goose.run', vscode.StatusBarAlignment.Left, 5);
    runStatus.name = 'Goose: Run with JIT';
    runStatus.text = '$(play) Run';
    runStatus.tooltip = 'Compile and run Goose with the JIT';
    runStatus.command = 'goose.run';
    function updateRunStatus() {
        if (vscode.workspace.isTrusted && vscode.window.activeTextEditor?.document.languageId === 'goose') runStatus.show();
        else runStatus.hide();
    }

    const jitConfigurations = () => [{ type: 'goose', request: 'launch', name: 'Goose: Run with JIT', noDebug: true }];
    // Participate in the native Run configuration picker without claiming a
    // debugger. Launch the same terminal task, then stop debug resolution.
    const launchProvider = {
        provideDebugConfigurations: jitConfigurations,
        resolveDebugConfiguration(_folder, config) {
            return { ...jitConfigurations()[0], ...config, noDebug: true };
        },
        async resolveDebugConfigurationWithSubstitutedVariables(folder, config) {
            if (!vscode.workspace.isTrusted) return undefined;
            if (config.request !== 'launch') {
                void vscode.window.showErrorMessage('Goose supports JIT launch only; attaching a debugger is not available.');
                return undefined;
            }
            const active = vscode.window.activeTextEditor?.document;
            const entry = (folder || active) && vscode.workspace.getConfiguration('goose', folder?.uri || active.uri).get('entryFile', '');
            let uri;
            if (config.program || entry) {
                const file = config.program || entry;
                const base = folder?.uri.fsPath || (active?.uri.scheme === 'file' ? path.dirname(active.uri.fsPath) : undefined);
                if (!base && !path.isAbsolute(file)) {
                    void vscode.window.showErrorMessage('Open a workspace folder or use an absolute Goose program path.');
                    return undefined;
                }
                uri = vscode.Uri.file(absolute(file, base || path.dirname(file), active?.uri.fsPath || ''));
            } else if (!folder || active && vscode.workspace.getWorkspaceFolder(active.uri)?.uri.toString() === folder.uri.toString()) {
                uri = (await savedActiveDocument())?.uri;
            }
            if (!uri) {
                void vscode.window.showErrorMessage('Open a Goose file or set goose.entryFile to run with JIT.');
                return undefined;
            }
            const definition = { type: 'goose', action: 'run', file: uri.fsPath };
            const cwd = settings(vscode, uri, definition, folder).cwd;
            for (const doc of vscode.workspace.textDocuments) {
                if (isGoose(doc) && doc.isDirty && cwdOf(doc) === cwd && !await doc.save()) return undefined;
            }
            await vscode.tasks.executeTask(makeTask(definition, uri, folder));
            return undefined;
        }
    };

    context.subscriptions.push(
        diagnostics, output, runStatus, checkStatus,
        { dispose() { disposed = true; queue.invalidate(); } },
        vscode.window.onDidChangeActiveTextEditor(() => { updateRunStatus(); updateCheckStatus(); }),
        vscode.workspace.onDidGrantWorkspaceTrust(() => { updateRunStatus(); updateCheckStatus(); }),
        vscode.debug.registerDebugConfigurationProvider('goose', launchProvider),
        vscode.debug.registerDebugConfigurationProvider('goose', { provideDebugConfigurations: jitConfigurations }, vscode.DebugConfigurationProviderTriggerKind.Dynamic),
        vscode.commands.registerCommand('goose.showOutput', () => output.show()),
        vscode.commands.registerCommand('goose.check', async () => {
            const doc = await savedActiveDocument();
            if (!doc) return;
            const completed = await queue.schedule(settings(vscode, doc.uri), 0);
            if (completed) output.show(true);
        }),
        ...['run', 'build', 'generateC'].map(action => vscode.commands.registerCommand(`goose.${action}`, async () => {
            const doc = await savedActiveDocument();
            if (doc) await vscode.tasks.executeTask(makeTask({ type: 'goose', action }, doc.uri));
        })),
        vscode.tasks.registerTaskProvider('goose', {
            provideTasks() {
                if (!vscode.workspace.isTrusted) return [];
                const doc = vscode.window.activeTextEditor?.document;
                if (doc && isGoose(doc)) return ['run', 'check', 'build', 'generateC'].map(action => makeTask({ type: 'goose', action }, doc.uri));
                // A configured program is runnable even when another editor (or
                // just the Run view) has focus.
                return (vscode.workspace.workspaceFolders || []).flatMap(folder => {
                    const entry = vscode.workspace.getConfiguration('goose', folder.uri).get('entryFile', '');
                    if (!entry) return [];
                    const uri = vscode.Uri.file(absolute(entry, folder.uri.fsPath, ''));
                    return ['run', 'check', 'build', 'generateC'].map(action => makeTask({ type: 'goose', action }, uri, folder));
                });
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
                    if (task.group) resolved.group = task.group;
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
            if (isGoose(doc)) refresh(cwdOf(doc));
        }),
        vscode.workspace.onDidChangeTextDocument(event => {
            if (isGoose(event.document) && event.contentChanges.length) queue.stale(cwdOf(event.document));
        }),
        vscode.workspace.onDidCloseTextDocument(doc => {
            if (isGoose(doc)) refresh(cwdOf(doc));
        }),
        // Results checked with other settings or workspace folders no longer apply.
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
        // A stale result is already due to be replaced, so it does not count.
        const existing = [...queue.results.values(), ...queue.jobs.values()].filter(item => item.config.cwd === cwd && !item.stale);
        if (existing.length && existing.every(item => item.startedAt === undefined || item.startedAt > modified)) return;
        refresh(cwd);
    };
    context.subscriptions.push(watcher, watcher.onDidCreate(changedOnDisk), watcher.onDidChange(changedOnDisk), watcher.onDidDelete(changedOnDisk));
    updateRunStatus();
    updateCheckStatus();
    recheck();
}

module.exports = { activate };
