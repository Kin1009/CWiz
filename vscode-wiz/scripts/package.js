'use strict';

// Build a .vsix without depending on @vscode/vsce.  A VSIX is an OPC zip:
//   [Content_Types].xml
//   extension.vsixmanifest
//   extension/...            (the extension itself)
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const root = path.resolve(__dirname, '..');
const pkg = JSON.parse(fs.readFileSync(path.join(root, 'package.json'), 'utf8'));
const out = path.join(root, `${pkg.name}-${pkg.version}.vsix`);
const staging = fs.mkdtempSync(path.join(require('os').tmpdir(), 'wiz-vsix-'));

const contentTypes = `<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension=".json" ContentType="application/json"/>
  <Default Extension=".js" ContentType="application/javascript"/>
  <Default Extension=".md" ContentType="text/markdown"/>
  <Default Extension=".ttf" ContentType="application/x-font-ttf"/>
  <Default Extension=".vsixmanifest" ContentType="text/xml"/>
</Types>
`;

const manifest = `<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0"
  xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011"
  xmlns:d="http://schemas.microsoft.com/developer/vsx-schema-design/2011">
  <Metadata>
    <Identity Language="en-US" Id="${pkg.name}" Version="${pkg.version}" Publisher="${pkg.publisher}" />
    <DisplayName>${pkg.displayName}</DisplayName>
    <Description xml:space="preserve">${pkg.description}</Description>
    <Tags>casio,classwiz,calculator,wiz,cwiz</Tags>
    <Categories>Programming Languages,Linters</Categories>
    <Properties>
      <Property Id="Microsoft.VisualStudio.Code.Engine" Value="${pkg.engines.vscode}" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionKind" Value="workspace" />
    </Properties>
  </Metadata>
  <Installation>
    <InstallationTarget Id="Microsoft.VisualStudio.Code"/>
  </Installation>
  <Dependencies/>
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true" />
  </Assets>
</PackageManifest>
`;

fs.writeFileSync(path.join(staging, '[Content_Types].xml'), contentTypes);
fs.writeFileSync(path.join(staging, 'extension.vsixmanifest'), manifest);

const dst = path.join(staging, 'extension');
fs.mkdirSync(dst);
for (const item of ['package.json', 'README.md', 'src', 'syntaxes', 'snippets',
                    'language-configuration-wiz.json', 'language-configuration-cwiz.json']) {
  fs.cpSync(path.join(root, item), path.join(dst, item), { recursive: true });
}

try { fs.unlinkSync(out); } catch {}
execFileSync('zip', ['-q', '-r', out, '.'], { cwd: staging, stdio: 'inherit' });
fs.rmSync(staging, { recursive: true, force: true });
console.log(out);
