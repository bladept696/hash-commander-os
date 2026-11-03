import { NbJSThemeOptions, COSMIC_THEME as baseTheme } from '@nebular/theme';

const baseThemeVariables = baseTheme.variables;

// Hash Commander OS: neon theme (purple / pink / green on near-black). Inherits from 'cosmic'.
export const HCOS_THEME = {
  name: 'hcos',
  base: 'cosmic',
  variables: {
    ...baseThemeVariables,
    fontMain: 'Inter, "Segoe UI", system-ui, sans-serif',
    bg: '#110e1d',
    bg2: '#151127',
    fgText: '#ece8ff',
    primary: '#b14dff',
  },
} as NbJSThemeOptions;
