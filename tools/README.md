# Icon generation

```powershell
cd tools
npm install
npm run icons
```

Output: `../icons/*.png` (64×64, transparent).

## Fonte

Lucide — https://lucide.dev — ISC.

O conjunto foi escolhido porque:

- é open-source e redistribuível;
- tem ícones de navegação consistentes;
- não depende de conta/atribuição por ícone.

## Flaticon

Os ícones do Flaticon **não** foram baixados automaticamente. Os termos variam
por autor/plano e muitos exigem atribuição. Se houver uma licença/arquivo
licenciado, basta trocar o `mapping` em `generate_icons.js` pelos PNG/SVG
baixados e rodar o gerador.

## VM icons

Os ícones do VirtualBox foram removidos de:

```text
icons/vbox_icons
resources/vbox_icons
build_deep.ps1
```

Nenhum ícone do VM é usado pela GUI.
