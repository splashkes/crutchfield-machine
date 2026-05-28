import { defineConfig } from 'vitepress'

// Vitepress config for crutchfield-machine documentation.
// The site is structured to mirror the `docs/` tree at the repo root —
// vitepress reads markdown from `..` (one level up) and treats `docs/`
// as the content root.

export default defineConfig({
  title: 'Crutchfield Machine',
  description: 'GPU video feedback machine — OSC, MIDI, Launch Control, math dashboard',
  base: '/crutchfield-machine/',          // adjust for your gh-pages path
  srcDir: '..',                            // serve from the repo root
  cleanUrls: true,

  themeConfig: {
    siteTitle: 'Crutchfield',
    logo: { src: '/gallery/01_shot.png', width: 24, height: 24 },

    socialLinks: [
      { icon: 'github', link: 'https://github.com/splashkes/crutchfield-machine' }
    ],

    nav: [
      { text: 'Get started',    link: '/docs/README' },
      { text: 'Features',       link: '/docs/features/MATHLAB' },
      { text: 'OSC',            link: '/docs/osc/ARCHITECTURE' },
      { text: 'Launch Control', link: '/docs/launch-control/V1_GUIDE' },
      { text: 'TouchDesigner',  link: '/docs/touchdesigner/GETTING_STARTED' },
      { text: 'Reference',      link: '/docs/osc/ACTIONS' }
    ],

    sidebar: {
      '/': [
        {
          text: 'Getting started',
          items: [
            { text: 'Overview',      link: '/docs/README' },
            { text: 'OSC reference', link: '/OSC_REFERENCE' }
          ]
        },
        {
          text: 'Features',
          collapsed: false,
          items: [
            { text: 'Hot reload',          link: '/docs/features/HOT_RELOAD' },
            { text: 'OSC echo',            link: '/docs/features/OSC_ECHO' },
            { text: 'Macros + snapshots',  link: '/docs/features/MACROS_SNAPSHOTS' },
            { text: 'Audio reactivity',    link: '/docs/features/AUDIO_REACTIVITY' },
            { text: 'Ableton Link',        link: '/docs/features/ABLETON_LINK' },
            { text: 'Syphon output',       link: '/docs/features/SYPHON' },
            { text: 'Mathlab dashboard',   link: '/docs/features/MATHLAB' }
          ]
        },
        {
          text: 'OSC',
          collapsed: false,
          items: [
            { text: 'Architecture',     link: '/docs/osc/ARCHITECTURE' },
            { text: 'Protocol',         link: '/docs/osc/PROTOCOL' },
            { text: 'Bindings',         link: '/docs/osc/BINDINGS' },
            { text: 'CLI + config',     link: '/docs/osc/CLI' },
            { text: 'Cookbook',         link: '/docs/osc/COOKBOOK' },
            { text: 'Action catalogue', link: '/docs/osc/ACTIONS' },
            { text: 'Troubleshooting',  link: '/docs/osc/TROUBLESHOOTING' }
          ]
        },
        {
          text: 'Launch Control',
          collapsed: false,
          items: [
            { text: 'LC v1 (original)', link: '/docs/launch-control/V1_GUIDE' },
            { text: 'LC XL',            link: '/docs/launch-control/XL_GUIDE' }
          ]
        },
        {
          text: 'TouchDesigner',
          collapsed: false,
          items: [
            { text: 'Getting started',   link: '/docs/touchdesigner/GETTING_STARTED' },
            { text: 'Build the network', link: '/development/touchdesigner/BUILD_NETWORK' },
            { text: 'LC → TD bridge',    link: '/development/touchdesigner/launch_control_xl_bridge' }
          ]
        }
      ]
    },

    footer: {
      message: 'MIT licensed',
      copyright: 'Crutchfield Machine — modern GPU video feedback'
    },

    search: {
      provider: 'local'
    }
  }
})
