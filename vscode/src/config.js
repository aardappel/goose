'use strict';

const fs = require('node:fs');
const path = require('node:path');

function expand(value, folder, file) {
    return value.replace(/\$\{workspaceFolder\}/g, () => folder)
        .replace(/\$\{file\}/g, () => file);
}

function absolute(value, folder, file) {
    return path.resolve(folder, expand(value, folder, file));
}

function compilerPath(configured, folder) {
    if (configured) {
        const expanded = expand(configured, folder, '');
        return /[\\/]/.test(expanded) ? path.resolve(folder, expanded) : expanded;
    }
    const name = process.platform === 'win32' ? 'goose.exe' : 'goose';
    for (const relative of [name, `build/Release/${name}`, `build/Debug/${name}`, `build/${name}`]) {
        const candidate = path.join(folder, relative);
        if (fs.existsSync(candidate) && fs.statSync(candidate).isFile()) return candidate;
    }
    return 'goose';
}

function settings(vscode, uri, definition = {}, taskFolder) {
    const workspaceFolder = taskFolder || vscode.workspace.getWorkspaceFolder(uri);
    const cwd = workspaceFolder ? workspaceFolder.uri.fsPath : path.dirname(uri.fsPath);
    const config = vscode.workspace.getConfiguration('goose', taskFolder?.uri || uri);
    const file = absolute(definition.file || config.get('entryFile', '') || uri.fsPath, cwd, uri.fsPath);
    return {
        workspaceFolder, cwd, file,
        compiler: compilerPath(config.get('compilerPath', ''), cwd),
        stdlib: config.get('stdlibPath', '') ? absolute(config.get('stdlibPath'), cwd, uri.fsPath) : '',
        timeout: Math.min(300000, Math.max(1000, config.get('checkTimeout', 15000))),
        checkOnSave: config.get('checkOnSave', true),
        runArguments: config.get('runArguments', []),
        output: definition.output ? absolute(definition.output, cwd, file) : file.replace(/(?:\.goose)?$/, '.c')
    };
}

function argumentsFor(action, config) {
    const args = config.stdlib ? ['--stdlib', config.stdlib] : [];
    if (action === 'check') args.push('--check');
    else if (action === 'run') args.push('--jit');
    else if (action === 'generateC') args.push('-o', config.output);
    else throw new Error(`Unknown Goose task action: ${action}`);
    args.push(config.file);
    if (action === 'run' && config.runArguments.length) args.push('--', ...config.runArguments);
    return args;
}

// Mirror StdlibDirs in src/main.cpp, including lookup next to a PATH executable.
function stdlibDirectories(config, env = process.env) {
    let executable = config.compiler;
    if (!path.isAbsolute(executable)) {
        const names = process.platform === 'win32' && !path.extname(executable)
            ? [`${executable}.exe`, executable] : [executable];
        executable = (env.PATH || '').split(path.delimiter)
            .flatMap(dir => names.map(name => path.resolve(dir, name)))
            .find(candidate => fs.existsSync(candidate)) || '';
    }
    const dirs = [config.stdlib, env.GOOSE_STDLIB].filter(Boolean)
        .map(dir => path.resolve(config.cwd, dir));
    if (executable) {
        for (let i = 0; i < 4; i++) {
            dirs.push(path.resolve(path.dirname(executable), ...Array(i).fill('..'), 'stdlib'));
        }
    }
    dirs.push(path.join(config.cwd, 'stdlib'));
    return [...new Set(dirs)];
}

module.exports = { absolute, compilerPath, settings, argumentsFor, stdlibDirectories };
