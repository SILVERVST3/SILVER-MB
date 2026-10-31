# SILVER-MB — Compressor Multibanda (VST3)

Compressor/expansor multibanda inspirado no FabFilter Pro-MB, com o visual prata / cinza claro e fontes azul-marinho da linha SILVER (mesma família do RefPro SILVER e do LPRO SILVER).

## Compilar no GitHub

1. Crie um repositório novo (ex.: `SILVER-MB`) e envie **todo o conteúdo desta pasta**, incluindo a pasta oculta `.github`.
2. O GitHub Actions compila sozinho a cada push (aba **Actions** → *Build SILVER-MB*). Também dá para rodar manualmente com **Run workflow**.
3. Ao terminar, baixe o artefato **SILVER-MB-VST3-Windows**, descompacte e copie a pasta `SILVER-MB.vst3` para `C:\Program Files\Common Files\VST3\`.
4. No REAPER: *Options → Preferences → Plug-ins → VST → Re-scan*.

Também é gerado um **SILVER-MB.exe** (standalone) para testes rápidos.

## Como usar

**No gráfico**
- **Duplo clique** em área vazia cria uma banda (até 6).
- **Arrastar o ponto** da banda: horizontal move a banda, vertical muda o ganho de saída da banda. O arraste trava num eixo; segure **Alt** para mover livre e **Shift** para ajuste fino.
- **Roda do mouse** sobre a banda: largura.
- **Alças na parte de baixo** (ou as bordas verticais da banda selecionada): arrastam o corte inferior/superior. As bordas **grudam** (snap) na borda das bandas vizinhas e nas extremidades de 20 Hz / 20 kHz; bordas grudadas se movem juntas, como um crossover.
- **Barrinha tracejada** (range): arraste na vertical para ajustar o Range.
- **Duplo clique no ponto**: zera o ganho. **Botão direito**: menu (Solo, Mute, Bypass, Compress/Expand, Excluir). **Delete**: apaga a banda selecionada.

**Painel da banda** — Threshold (o anel azul claro mostra o nível do detector), Range, Compress/Expand, Ratio, Knee, Attack, Release, Lookahead, Stereo Link, modo Stereo/Mid/Side, Output, cortes e inclinações (6 a 48 dB/oct), Solo/Mute/Bypass.

Os quatro comportamentos, como no Pro-MB:

| Modo | Range negativo | Range positivo |
|---|---|---|
| Compress | compressão para baixo (corta picos acima do threshold) | compressão para cima (levanta o que está abaixo) |
| Expand | expansão para baixo / gate | expansão para cima (realça transientes) |

**Expert** — detector em faixa livre (Free + Detect Low/High), sidechain externo (Ext SC) e Audition (ouvir o detector). No REAPER, o sidechain entra pelos canais 3/4 da trilha.

**Barra inferior** — modo de processamento, Lookahead global, analisador (Pre/Post/SC, Freeze, resolução, velocidade, inclinação de 4,5 dB/oct), escala do gráfico (±3/6/12/30 dB), Input, Mix (0–200%) e Output. No topo: Undo/Redo e comparação A/B.

## Modos de processamento

- **Dynamic Phase** (padrão, latência zero além do lookahead): cada banda é um par de shelves Butterworth recíprocos cujo ganho muda amostra a amostra. Com ganho 0 dB a banda é transparente (nenhuma mudança de fase); bandas grudadas com o mesmo ganho se juntam perfeitamente.
- **Linear Phase**: filtros FIR de fase linear com cortes complementares (bandas vizinhas somam exatamente 1). Latência extra de 2.560 amostras a 44,1/48 kHz.

Latência: 20 ms quando o Lookahead global está ligado; desligado, o modo Dynamic Phase fica com latência zero.

## Ainda não implementado (comparado ao Pro-MB)

Oversampling, modo Minimum Phase, presets por banda, MIDI Learn e o modo de exibição em tela cheia.

## Estrutura

```
Source/Params.*            parâmetros e matemática compartilhada
Source/DSP/Filters.h       SVF TPT, shelves dinâmicos, cadeias Butterworth, FIR de fase linear
Source/PluginProcessor.*   motor multibanda, detector, lookahead, sidechain, A/B
Source/UI/*                LookAndFeel prata, gráfico interativo, painel da banda
.github/workflows/build.yml
```
