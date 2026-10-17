import { defineConfig } from 'vite'

export default defineConfig({
  // motion reads process.env.NODE_ENV, which doesn't exist in the browser.
  define: {
    'process.env.NODE_ENV': JSON.stringify('production'),
  },
  build: {
    lib: {
      entry: './browserify.js',
      name: '_encomBundle',
      formats: ['iife'],
      fileName: () => 'encom-globe.js',
    },
    outDir: 'build',
    emptyOutDir: false,
    minify: false,
    commonjsOptions: {
      include: [/.*/],
      transformMixedEsModules: true,
    },
    rollupOptions: {
      output: {
        strict: false,
      },
    },
  },
})
