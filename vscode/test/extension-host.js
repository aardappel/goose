'use strict';

// Run with --extensionTestsPath=<this file> in an isolated, trusted workspace.
// The workspace is a disposable test fixture, not the user's Goose project.
const vscode = require('vscode');
const assert = require('node:assert/strict');
const path = require('node:path');
const fs = require('node:fs/promises');
const { compilerPath } = require('../src/config');

async function until(predicate, description) {
    const deadline = Date.now() + 15000;
    while (Date.now() < deadline) {
        if (await predicate()) return;
        await new Promise(resolve => setTimeout(resolve, 50));
    }
    throw new Error(`Timed out: ${description}`);
}

async function run() {
    const folder = vscode.workspace.workspaceFolders?.[0];
    assert.ok(folder && path.basename(folder.uri.fsPath) === 'goose-extension-fixture', 'Use an isolated goose-extension-fixture workspace');
    assert.ok(vscode.workspace.isTrusted);
    await fs.mkdir(path.join(folder.uri.fsPath, '.vscode'), { recursive: true });
    await fs.writeFile(path.join(folder.uri.fsPath, '.vscode', 'tasks.json'), JSON.stringify({
        version: '2.0.0', tasks: [{ type: 'goose', label: 'Fixture check', action: 'check', file: 'main.goose', problemMatcher: '$goose' }]
    }));
    const root = path.resolve(__dirname, '../..');
    const config = vscode.workspace.getConfiguration('goose', folder.uri);
    await config.update('compilerPath', process.env.GOOSE_TEST_COMPILER || compilerPath('', root), vscode.ConfigurationTarget.WorkspaceFolder);
    await config.update('entryFile', 'main.goose', vscode.ConfigurationTarget.WorkspaceFolder);
    await config.update('checkOnSave', false, vscode.ConfigurationTarget.WorkspaceFolder);
    const main = vscode.Uri.joinPath(folder.uri, 'main.goose');
    const helper = vscode.Uri.joinPath(folder.uri, 'helper.goose');
    await fs.writeFile(main.fsPath, 'import helper;\nfn main() { helper(); }\n');
    await fs.writeFile(helper.fsPath, 'fn helper() { let x = 1; x = 2; }\n');
    const doc = await vscode.workspace.openTextDocument(main);
    assert.equal(doc.languageId, 'goose');
    await vscode.window.showTextDocument(doc);
    const extension = vscode.extensions.getExtension('aardappel.goose-language');
    assert.ok(extension);
    await extension.activate();
    await vscode.commands.executeCommand('goose.check');
    const problems = () => vscode.languages.getDiagnostics(helper).filter(item => item.source === 'Goose');
    await until(() => problems().length === 1, 'compiler error in imported file');
    await new Promise(resolve => setTimeout(resolve, 600));
    assert.equal(problems().length, 1, 'late filesystem notifications must preserve manual check results');
    assert.ok(problems()[0].range.end.character > problems()[0].range.start.character);
    assert.ok(problems()[0].relatedInformation.some(item => item.location.uri.fsPath === main.fsPath));

    const symbols = await vscode.commands.executeCommand('vscode.executeDocumentSymbolProvider', main);
    assert.ok(symbols.some(item => item.name === 'main'));
    const links = await vscode.commands.executeCommand('vscode.executeLinkProvider', main);
    assert.ok(links.some(link => link.target.fsPath === helper.fsPath));
    const tasks = await vscode.tasks.fetchTasks({ type: 'goose' });
    for (const action of ['check', 'run', 'generateC']) {
        const task = tasks.find(item => item.definition.action === action);
        assert.ok(task, action);
        assert.ok(task.execution instanceof vscode.ProcessExecution);
        assert.ok(task.execution.args.includes(main.fsPath));
    }
    const configured = tasks.find(task => task.name === 'Fixture check');
    assert.ok(configured, 'configured task is resolved');
    assert.ok(configured.execution.args.includes('--check'));
    const matchers = extension.packageJSON.contributes.problemMatchers;
    const matcher = new RegExp(matchers[0].pattern.regexp);
    assert.equal(matcher.exec(`${helper.fsPath}:1: error: test`)[1], helper.fsPath);

    const helperDoc = await vscode.workspace.openTextDocument(helper);
    await vscode.window.showTextDocument(helperDoc);
    await config.update('checkOnSave', true, vscode.ConfigurationTarget.WorkspaceFolder);
    const replace = async text => {
        const edit = new vscode.WorkspaceEdit();
        edit.replace(helper, new vscode.Range(helperDoc.positionAt(0), helperDoc.positionAt(helperDoc.getText().length)), text);
        assert.ok(await vscode.workspace.applyEdit(edit));
    };
    await replace('fn helper() { var x = 1; x = 2; }\n');
    await until(() => problems().length === 0, 'edit clears outdated error');
    assert.ok(await helperDoc.save());
    await vscode.commands.executeCommand('goose.check');
    assert.equal(problems().length, 0);
    await replace('fn helper() { let x = 1; x = 2; }\n');
    assert.ok(await helperDoc.save());
    await until(() => problems().length === 1, 'save rechecks configured entry file');
    await config.update('checkOnSave', false, vscode.ConfigurationTarget.WorkspaceFolder);
    await until(() => problems().length === 0, 'disabling checks clears errors');

    console.log('Goose extension host: activation, imports, outline, tasks, diagnostics and save lifecycle passed.');
}

module.exports = { run };
