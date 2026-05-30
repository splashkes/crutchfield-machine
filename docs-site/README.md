# Docs site (Vitepress)

The Markdown lives at the repo root under `docs/`. Vitepress reads from
there with `srcDir: '..'`, so editing the docs and editing the published
site are the same workflow.

## Local preview

```bash
cd docs-site
npm install
npm run dev          # opens http://localhost:5173
```

## Production build

```bash
cd docs-site
npm install
npm run build        # outputs to .vitepress/dist/
```

## Publish to GitHub Pages

There's a workflow at `.github/workflows/docs.yml` (added in a follow-up
commit) that builds + deploys to `gh-pages`. For one-off manual deploys:

```bash
cd docs-site
npm run build
# then copy .vitepress/dist/ to your gh-pages branch
```
