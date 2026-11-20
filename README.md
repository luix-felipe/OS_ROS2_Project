# FleetPOSIX Workspace

Workspace ROS 2 Jazzy do projeto academico FleetPOSIX: uma central POSIX
controla tres AMRs independentes, representados por tartarugas, em uma regiao
critica compartilhada.

O ROS 2/Turtlesim e usado para pose, movimento e visualizacao. A orquestracao
usa explicitamente `fork`, `exec`, `waitpid`, pthreads, mutexes, variavel de
condicao, semaforos, pipe, filas POSIX, memoria compartilhada e sinais.

## Comecar pelo Makefile

Abra a raiz deste repositorio em um terminal Ubuntu/WSL. Nao e preciso executar
`source` antes dos alvos Make: cada tarefa carrega o ROS e, quando necessario,
o overlay do workspace. O Make apenas orquestra `colcon`, CMake e as ferramentas
ROS; a arquitetura POSIX do programa continua a mesma.

```bash
make                 # Ajuda completa; tambem pode usar make help.
make status          # Andamento documentado e artefatos desta copia.
make env             # Diagnostico de ferramentas e pacotes ROS instalados.
make verify          # Ambiente -> build -> estilo -> testes, sem abrir janela.
make simulate        # Comparacao segura/insegura offscreen com logs e resumo.
```

Depois da validacao, escolha uma execucao visual:

```bash
make demo            # Demo segura no WSLg: tres missoes, zero colisoes.
make demo-unsafe     # Demo insegura: pelo menos uma colisao logica esperada.
make run             # Operacao online segura, por comandos no terminal.
make run-unsafe      # Operacao online sem exclusao mutua do corredor.
```

No PowerShell, pode chamar os mesmos alvos com `wsl -d Ubuntu -- make help`
ou `wsl -d Ubuntu -- make verify`, estando na pasta deste repositorio. As tarefas
de build e simulacao devem executar no Linux: o compilador nativo do Windows
nao fornece os IPCs POSIX usados pelo projeto.

## O que cada automacao faz

| Alvo | Comportamento e resultado |
| --- | --- |
| `make help` | Mostra os comandos, a sequencia sugerida e as variaveis configuraveis. E o alvo padrao. |
| `make status` | Le o andamento desta documentacao e aponta overlay, resultados CTest e logs locais existentes. |
| `make env` | Confere ferramentas, seis pacotes ROS e acesso a `/dev/shm` e `/dev/mqueue`. Opcionais ausentes geram avisos. |
| `make deps` | Executa `rosdep check`; falha se rosdep, sua base ou dependencias estiverem ausentes. Nao instala nada. |
| `make build` | Compila com `colcon build --symlink-install`, testes habilitados e `-Werror` preservado. |
| `make lint` | Verifica estilo C++ em fontes, headers e testes, sem modificar arquivos. |
| `make format` | Aplica `ament_uncrustify --reformat` e confere o resultado. Modifica fontes; revise o diff. |
| `make test` | Compila, roda CTest offscreen e imprime `colcon test-result --verbose`. Falhas sao propagadas. |
| `make report` | Le os resultados ja registrados. Exige os dois resultados esperados; nao executa novos testes. |
| `make verify` | Encadeia ambiente, build, estilo e testes. Compila uma vez e interrompe na primeira etapa que falhar. |
| `make simulate` | Compila e executa primeiro seguro e depois inseguro, sem janela, com timeout e logs separados. |
| `make demo` / `make demo-unsafe` | Compilam e abrem a demonstracao correspondente no WSLg. |
| `make run` / `make run-unsafe` | Compilam e iniciam a operacao online correspondente com visualizacao. |
| `make ipc` | Lista processos e objetos `fleet_posix_*` da maquina, incluindo possiveis outras execucoes. Nao remove recursos. |
| `make slides` | Regenera o PDF de seis paginas; requer `reportlab` no Python selecionado. |
| `make clean` | Remove apenas `build/`, `install/` e `log/` desta copia, incluindo resultados e logs locais. Recusa links simbolicos nesses destinos. |

O `make verify` ja executa as duas demonstracoes por meio da suite de integracao.
`make simulate` serve para acompanhar a comparacao isoladamente, com os logs
da frota visiveis no terminal. Nenhum dos dois precisa de WSLg. As tarefas
visuais ficam fora da verificacao automatica e exigem `DISPLAY` ou
`WAYLAND_DISPLAY`. Na operacao online, use `sair` ou `Ctrl+C` para encerrar.

As tarefas param quando uma ferramenta falha: um build com erro impede os
testes, uma divergencia de estilo reprova `lint`, e um timeout ou resultado
inesperado reprova `simulate`. `make report` consulta resultados anteriores;
apenas `make test`/`make verify` atualizam a evidencia de testes. Nenhum alvo
instala pacotes, executa `sudo`, altera licenca ou publica no Git.

### Configuracao

| Variavel | Padrao | Uso |
| --- | --- | --- |
| `ROS_DISTRO` | `jazzy` | Distribuicao usada na descoberta do setup e no rosdep. Ambiente de referencia: Jazzy. |
| `ROS_SETUP` | `/opt/ros/$(ROS_DISTRO)/setup.bash` | Arquivo do ambiente ROS; pode apontar para outra instalacao. |
| `ROS_DOMAIN_ID` | `0` | Dominio das simulacoes. Os testes mantem seu dominio separado. |
| `JOBS` | `2` | Paralelismo de compilacao CMake e numero de workers do colcon. Deve ser inteiro positivo. |
| `DEMO_TIMEOUT` | `45s` | Limite externo por demo offscreen em `make simulate`; aceita a sintaxe do GNU timeout. |
| `PYTHON` | `python3` | Python do diagnostico opcional e da geracao dos slides. O build descobre seu Python via CMake. |

Exemplo para executar a comparacao em um dominio ROS separado:

```bash
make simulate ROS_DOMAIN_ID=171 JOBS=2 DEMO_TIMEOUT=45s
```

O Make serializa os alvos mesmo com `make -j`, para que build, teste e simulacao
nao disputem a mesma copia. O script auxiliar descobre a raiz por sua propria
localizacao e preserva caminhos com espacos, como o caminho deste repositorio
no Windows/WSL. Os arquivos de texto usam LF.

### Logs e diagnostico

- `log/automation/test.log`: saida da ultima suite executada por Make.
- `log/automation/demo-safe.log` e `demo-unsafe.log`: ultima comparacao offscreen.
- `build/fleet_posix/test_logs/`: logs individuais dos cenarios de integracao.
- `make ipc`: inspecao de processos e memoria/filas POSIX apos a execucao.

O ambiente atual permite compilar e testar, mas ainda nao possui `rosdep`
instalado. Por isso `make env` avisa e `make deps` falha explicitamente. A
verificacao rosdep e separada de `make verify` e continua pendente ate a
ferramenta e sua base estarem disponiveis. `reportlab` tambem e opcional e
necessario apenas para regenerar os slides; o PDF pronto esta no repositorio.

## Andamento do projeto

<!-- andamento:inicio -->
Referencia validada em 2026-11-20. O quadro descreve o projeto; a existencia de
artefatos locais nao comprova que o codigo atual passou nas verificacoes.

| Etapa | Situacao | Evidencia ou proximo passo |
| --- | --- | --- |
| MVP e APIs POSIX | Implementado | Tres AMRs, processos, pthreads, sincronizacao e IPC explicitos. |
| Build e estilo C++ | Validados | GCC 13, C++17, -Werror e ament_uncrustify. |
| Testes sem janela | Validados | 11 cenarios Python + teste C++ de mutex robusto; use make test. |
| Comparacao segura/insegura | Validada | 3 missoes por modo; 0 colisoes no seguro, 2 no inseguro na referencia. |
| WSLg e operacao online | Validados | Demo visual e shutdown com pausa/corredor ocupado exercitados. |
| Automacao Make | Validada | make -j4 verify e make simulate aprovados; propagacao de falhas e limpeza conferidas. |
| Apresentacao | Pronta | Seis paginas em docs/FleetPOSIX_apresentacao.pdf. |
| Dependencias via rosdep | Pendente no ambiente | Instalar/inicializar rosdep e executar make deps. |
| Dados da equipe e licenca | Pendente de definicao | Preencher package.xml e documentar a divisao de trabalho. |
<!-- andamento:fim -->

Para acompanhar outra copia ou apos alterar os fontes, execute `make status`
e depois `make verify`. O quadro de referencia deve ser atualizado apenas
depois de repetir as validacoes relevantes.

A documentacao funcional, os comandos manuais equivalentes e o mapeamento
para a disciplina estao em [`src/fleet_posix/README.md`](src/fleet_posix/README.md).

## Material de entrega

- Apresentacao de seis paginas: [`docs/FleetPOSIX_apresentacao.pdf`](docs/FleetPOSIX_apresentacao.pdf).
- Gerador editavel dos slides: [`docs/generate_slides.py`](docs/generate_slides.py).
- Mantenedor, email e licenca ainda precisam ser definidos pela equipe em `package.xml`.
- A equipe deve registrar os integrantes e sua divisao de trabalho antes da entrega.

Os slides incluem os resultados de `make verify`, o andamento e as pendencias,
e o roteiro Make para conferir o ambiente, testar e executar as simulacoes.

Para regenerar a apresentacao, execute `python docs/generate_slides.py` em um
ambiente com `reportlab` instalado. Essa dependencia e exclusiva do material
de apresentacao; nao e necessaria para compilar ou executar o projeto.
