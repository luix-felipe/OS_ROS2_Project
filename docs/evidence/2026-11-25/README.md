# Validacao de entrega — 25/11/2026

Revalidacao no Ubuntu 24.04/WSL 2 com ROS 2 Jazzy, GCC 13.3 e C++17. Os
arquivos desta pasta registram a execucao de entrega, separadamente das
capturas e midias gravadas em 23/11/2026.

## Comando executado

```bash
make -j4 verify
```

O fluxo terminou com codigo zero: ambiente, build com `-Werror`, estilo C++
e as duas entradas CTest aprovados. A suite Python executou 12 cenarios e o
teste C++ verificou a recuperacao do mutex compartilhado apos morte do dono:
**13 cenarios no total**.

## Resultados

| Verificacao | Resultado |
| --- | --- |
| Compilacao | Concluida com `BUILD_TESTING=ON` e avisos tratados como erro |
| Estilo C++ | `ament_uncrustify` sem divergencias |
| Teste C++ do mutex robusto | Aprovado |
| Integracao Python | 12 cenarios aprovados |
| Demo segura offscreen, executada pela suite | 3 missoes concluidas, 0 colisoes |
| Demo insegura offscreen, executada pela suite | 3 missoes concluidas, 2 colisoes logicas |
| Shutdown, sinais, FIFO e falhas parciais | Aprovados pelos cenarios de integracao |

Os testes verificam a ausencia de processos e IPCs proprios antes de sua
limpeza de contingencia. Os logs seguro e inseguro abaixo foram copiados
dos cenarios executados por essa suite; nao sao uma nova gravacao visual.

## Arquivos

- `environment.log`: diagnostico do ambiente e avisos de ferramentas opcionais;
- `build.log`: saida do build colcon correspondente;
- `style.log`: verificacao de estilo sem alteracoes nos fontes;
- `tests.log`: CTest, os 12 cenarios Python e o resumo dos resultados;
- `safe.log` e `unsafe.log`: saida das duas demos offscreen da suite;
- `validation.json`: versao consultada, hashes dos fontes e resultados registrados.

Para reproduzir a partir da raiz do repositorio:

```bash
make verify
make simulate
make ipc
```

`make simulate` retorna erro se os tres AMRs nao concluirem as missoes ou
se a contagem nao corresponder ao modo. A expectativa insegura e pelo menos
uma colisao; duas foi o resultado observado nesta validacao.

## Verificacoes separadas

- `rosdep check` nao foi executado: rosdep nao esta instalado nesta maquina.
  O build encontrou os pacotes CMake exigidos. `make deps` permite repetir
  essa verificacao em um ambiente com rosdep e sua base disponiveis.
- O diagnostico registrado inclui o antigo gerador local de PDF e seu aviso
  sobre `reportlab`. A apresentacao oficial passou a ser mantida no Canva,
  conforme [`../../README.md`](../../README.md); essa dependencia nao faz
  parte da execucao atual do projeto.
- Esta validacao e sem janela. Para evidencias visuais, consulte
  [`../2026-11-23/README.md`](../2026-11-23/README.md); para executar com janela,
  use `make demo` e `make demo-unsafe` em uma sessao WSLg.
