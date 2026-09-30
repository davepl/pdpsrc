// Run on a modern development machine. The PDP serves the committed result.
import {readFile, writeFile} from 'node:fs/promises';
import {minify} from 'html-minifier-terser';

const source = new URL('site/index.source.html', import.meta.url);
const destination = new URL('site/index.html', import.meta.url);
const input = await readFile(source, 'utf8');
const output = await minify(input, {
  collapseWhitespace: true,
  decodeEntities: true,
  removeComments: true,
  removeOptionalTags: true,
  removeRedundantAttributes: true,
  removeEmptyAttributes: true,
  removeAttributeQuotes: true,
  sortAttributes: true,
  sortClassName: true,
  // Level 2 incorrectly drops the responsive clamp() font shorthand.
  minifyCSS: {level: 1},
  minifyJS: {compress: {passes: 2}, mangle: true},
});

if (process.argv.includes('--check')) {
  if (await readFile(destination, 'utf8') !== output) {
    throw new Error('site/index.html is stale; run npm run build.');
  }
} else {
  await writeFile(destination, output);
}
console.log(`Homepage: ${Buffer.byteLength(input)} -> ${Buffer.byteLength(output)} bytes (HTML, CSS, and JavaScript minified).`);
