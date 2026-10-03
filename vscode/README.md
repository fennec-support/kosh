# Koshka for VS Code

This extension runs the Koshka language server and formatter. The server is
part of the shell binary.

## Building and installing

```bash
npm install
npm run compile
npm run package
code --install-extension kosh.vsix
```

## Formatting on save

Formatting on save is a user setting. The extension does not enable it.

```jsonc
{
  "[shellscript]": {
    "editor.formatOnSave": true
  }
}
```

Leave `editor.formatOnSaveMode` at its default value of `file`. The server
formats whole documents and advertises no range formatting. The
`modifications` mode skips the file and reports nothing.

## Settings

Add these to `settings.json`. The values shown are the defaults.

```jsonc
{
  // The Koshka language server is started when this is true.
  "kosh.enable": true,

  // This is the name or the absolute path of the kosh binary. A bare name is
  // searched in PATH.
  "kosh.path": "kosh",

  // These arguments are placed before --as-language-server.
  "kosh.arguments": [],

  // The messages exchanged with the server are traced at this level. The
  // levels are "off", "messages", and "verbose".
  "kosh.trace.server": "off"
}
```

The server reads its configuration once at startup. A change to any of the
options triggers a restart.
