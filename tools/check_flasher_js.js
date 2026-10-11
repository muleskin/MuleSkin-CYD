#!/usr/bin/env node
// MuleSkin-CYD — do the web flasher's pages still parse?
//
//   node tools/check_flasher_js.js
//
// The flasher is one HTML file per page with its script inline, and nothing
// builds it: a stray brace or a broken regex is a blank, dead page found only
// after a release is deployed. This compiles every inline <script> (not run --
// there is no browser here) and fails on the first that will not parse. CI
// runs it beside the host tests.
'use strict';
const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..', 'web-flasher');
const pages = [];
(function walk(dir) {
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) walk(p);
    else if (e.name.endsWith('.html')) pages.push(p);
  }
})(root);

let bad = 0, scripts = 0;
for (const page of pages) {
  const html = fs.readFileSync(page, 'utf8');
  // Inline scripts only: <script> or <script type="module"> without a src.
  const re = /<script(?![^>]*\bsrc=)([^>]*)>([\s\S]*?)<\/script>/gi;
  let m, i = 0;
  while ((m = re.exec(html))) {
    i++;
    scripts++;
    const isModule = /type\s*=\s*["']module["']/i.test(m[1]);
    try {
      if (isModule) {
        // new Function cannot hold import/export; strip them for the syntax check.
        new Function(m[2].replace(/^\s*(import|export)\b[^\n]*$/gm, ''));
      } else {
        new Function(m[2]);
      }
    } catch (err) {
      bad++;
      console.log('FAIL ' + path.relative(root, page) + ' script ' + i + ': ' + err.message);
    }
  }
}
console.log(bad ? bad + ' of ' + scripts + ' scripts do not parse' :
                  'all ' + scripts + ' inline scripts parse (' + pages.length + ' pages)');
process.exit(bad ? 1 : 0);
