"use strict";

// Package from a temporary staging directory that holds only what the extension
// needs at run time. The language server is a separate executable (goose-tools).
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { spawnSync } = require("node:child_process");

const here = __dirname;
const args = process.argv.slice(2);
if (args.length && (args.length !== 2 || args[0] !== "--out")) {
  console.error("Usage: npm run package -- [--out path.vsix]");
  process.exit(2);
}
const output = path.resolve(here, args[1] || "goose-language.vsix");
const staging = fs.mkdtempSync(path.join(os.tmpdir(), "goose-vsix-"));
function run(command, arguments_, cwd) {
  const result = spawnSync(command, arguments_, {
    cwd,
    stdio: "inherit",
    shell: false,
  });
  if (result.error) throw result.error;
  if (result.status !== 0)
    throw new Error(`${command} failed (${result.status ?? result.signal})`);
}
try {
  run(
    process.execPath,
    [
      "--test",
      ...fs
        .readdirSync(path.join(here, "test"))
        .filter((file) => file.endsWith(".test.js"))
        .map((file) => path.join(here, "test", file)),
    ],
    here,
  );
  for (const name of [
    "package.json",
    "README.md",
    ".vscodeignore",
    "language-configuration.json",
    "src",
    "syntaxes",
    "snippets",
    "fileicons",
  ]) {
    fs.cpSync(path.join(here, name), path.join(staging, name), {
      recursive: true,
    });
  }
  fs.copyFileSync(
    path.resolve(here, "../LICENSE"),
    path.join(staging, "LICENSE"),
  );
  const manifestPath = path.join(staging, "package.json");
  const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf8"));
  // The icon lives in the repository's docs, but the package must carry its own copy.
  if (manifest.icon) {
    const icon = path.resolve(here, manifest.icon);
    if (!fs.existsSync(icon)) throw new Error(`icon not found: ${manifest.icon}`);
    fs.mkdirSync(path.join(staging, "media"));
    fs.copyFileSync(icon, path.join(staging, "media/icon.png"));
    manifest.icon = "media/icon.png";
  }
  // Tests ran against the checkout above. Staging has runtime assets only.
  delete manifest.scripts;
  delete manifest.devDependencies;
  fs.writeFileSync(manifestPath, JSON.stringify(manifest, null, 2) + "\n");
  run(
    process.execPath,
    [
      path.join(here, "node_modules/@vscode/vsce/vsce"),
      "package",
      "--no-dependencies",
      "--out",
      output,
    ],
    staging,
  );
} catch (error) {
  console.error(`VSIX build failed: ${error.message}`);
  process.exitCode = 1;
} finally {
  fs.rmSync(staging, { recursive: true, force: true });
}
