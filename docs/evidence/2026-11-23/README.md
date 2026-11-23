# Evidencias visuais — 2026-11-23

Capturas reais do Turtlesim executando a revisao presente em
`revisao-fleetposix.patch`, posteriormente incorporada ao branch `main`.

## Resultado observado

| Modo | Resultado |
| --- | --- |
| Seguro | 3 missoes concluidas, travessias serializadas e 0 colisoes logicas |
| Inseguro | 3 missoes concluidas e 2 colisoes logicas detectadas |

Arquivos:

- `safe-corredor-1.png`: AMRs aguardando enquanto um agente ocupa o corredor;
- `safe-corredor-2.png`: segunda travessia serializada no cruzamento;
- `unsafe-convergencia.png`: tres AMRs convergindo sem exclusao mutua;
- `unsafe-cruzamento.png`: sobreposicao visual no centro da fabrica abstrata;
- `safe.log` e `unsafe.log`: saida integral do supervisor nas duas execucoes.

## Comandos equivalentes

```bash
make demo ROS_DOMAIN_ID=187
make demo-unsafe ROS_DOMAIN_ID=188
```

As imagens sao capturas brutas da janela de 500 × 500 pixels do Turtlesim,
sem recomposicao dos robos ou das trajetorias. O Turtlesim e uma abstracao 2D;
as colisoes sao detectadas pela distancia entre poses no supervisor.
