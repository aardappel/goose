'use strict';

const path = require('node:path');
const { execFile } = require('node:child_process');
const { argumentsFor } = require('./config');

function parseDiagnostics(output, cwd) {
    const lines = output.replace(/\x1b\[[0-9;]*m/g, '').split(/\r?\n/);
    const diagnostics = [];
    let current;
    for (let i = 0; i < lines.length; i++) {
        const match = /^(.+?):(\d+)(?::(\d+))?: (error|warning): (.*)$/.exec(lines[i]);
        if (match) {
            current = {
                file: path.resolve(cwd, match[1]), line: Math.max(0, Number(match[2]) - 1),
                column: match[3] ? Math.max(0, Number(match[3]) - 1) : undefined,
                severity: match[4], message: match[5], related: []
            };
            const caret = /^[\t ]*\^~*\s*$/.exec(lines[i + 2] || '');
            if (caret) {
                // Lexer carets count UTF-8 bytes, preserving tabs as single characters.
                current.column = Buffer.from(lines[i + 1], 'utf8')
                    .subarray(0, caret[0].indexOf('^')).toString('utf8').length;
            }
            diagnostics.push(current);
        } else {
            const frame = /^\s+in (.+) instantiated from (.+):(\d+)$/.exec(lines[i]);
            if (frame && current) current.related.push({
                message: `Instantiated ${frame[1]}`, file: path.resolve(cwd, frame[2]),
                line: Math.max(0, Number(frame[3]) - 1)
            });
        }
    }
    return diagnostics;
}

// Progress lines the compiler prints on success (parsed ok, typechecked ok, ...).
const statusLine = /^(?:parsed ok|roundtrip ok|typechecked ok|bce|bce-line|bce-test):|^wrote .+ \(\d+ bytes\)$/;

// The text of a failure that has no source location, without the progress lines.
function failureMessage(output) {
    return output.replace(/\x1b\[[0-9;]*m/g, '').split(/\r?\n/)
        .filter(line => !statusLine.test(line)).join('\n').trim();
}

function runCompiler(config) {
    let child;
    const promise = new Promise(resolve => {
        child = execFile(config.compiler, argumentsFor('check', config), {
            cwd: config.cwd, timeout: config.timeout, maxBuffer: 8 * 1024 * 1024,
            encoding: 'utf8', windowsHide: true, shell: false
        }, (error, stdout, stderr) => resolve({ error, output: [stdout, stderr].filter(Boolean).join('\n') }));
    });
    return { promise, cancel: () => child.kill() };
}

// Own results per checked entry file: clearing one root must not erase another's
// errors in a shared import. A generation token prevents late checks resurfacing.
// A result stays until the next check of its entry replaces it, marked stale
// once its sources change; one that no longer applies is dropped instead.
class CheckQueue {
    constructor(check, changed) {
        this.check = check;
        this.changed = changed;
        this.jobs = new Map();
        this.results = new Map();
    }

    cancel(key) {
        const job = this.jobs.get(key);
        if (!job) return false;
        clearTimeout(job.timer);
        job.run?.cancel();
        job.finish(false);
        this.jobs.delete(key);
        return true;
    }

    // The checks and results in cwd (or everywhere) no longer apply.
    invalidate(cwd) {
        for (const [key, job] of this.jobs) {
            if (!cwd || job.config.cwd === cwd) this.cancel(key);
        }
        for (const [key, result] of this.results) {
            if (!cwd || result.config.cwd === cwd) this.results.delete(key);
        }
        this.changed(this.results);
    }

    // Sources in cwd changed: stop its checks, and keep its results, marked
    // stale, for the next checks to replace.
    stale(cwd) {
        let changed = false;
        for (const [key, job] of this.jobs) {
            if (job.config.cwd === cwd) changed = this.cancel(key) || changed;
        }
        for (const result of this.results.values()) {
            if (result.config.cwd === cwd && !result.stale) result.stale = changed = true;
        }
        if (changed) this.changed(this.results);
    }

    // Drop the results of entry files that are no longer checked.
    retain(checked) {
        let changed = false;
        for (const key of this.results.keys()) {
            if (!checked(key)) changed = this.results.delete(key);
        }
        if (changed) this.changed(this.results);
    }

    schedule(config, delay = 180) {
        const key = config.file;
        this.cancel(key);
        const scheduled = new Promise(finish => {
            const job = { config, finish };
            this.jobs.set(key, job);
            job.timer = setTimeout(async () => {
                try {
                    job.startedAt = Date.now();
                    job.run = this.check(config);
                    const result = await job.run.promise;
                    if (this.jobs.get(key) !== job) return;
                    this.jobs.delete(key);
                    this.results.set(key, { config, startedAt: job.startedAt, ...result });
                    this.changed(this.results);
                    finish(true);
                } catch (error) {
                    if (this.jobs.get(key) !== job) return;
                    this.jobs.delete(key);
                    this.results.set(key, { config, startedAt: job.startedAt, error, output: '' });
                    this.changed(this.results);
                    finish(true);
                }
            }, delay);
        });
        // Report after the job is registered, so a listener can see it is running.
        this.changed(this.results);
        return scheduled;
    }
}

module.exports = { parseDiagnostics, failureMessage, runCompiler, CheckQueue };
