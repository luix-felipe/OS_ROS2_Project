# FleetPOSIX

Projeto da disciplina de Sistemas Operacionais: uma central POSIX coordena
tres robos moveis autonomos (AMRs) independentes que disputam uma regiao
critica compartilhada (o corredor central de um almoxarifado). Os robos sao
visualizados como tartarugas no Turtlesim (ROS 2 Jazzy).

## Objetivo

Demonstrar, em uma aplicacao executavel e reprodutivel, programacao
concorrente, sincronizacao e comunicacao entre processos com as APIs POSIX
explicitas. O ROS 2 fica restrito a pose, movimento e visualizacao; todo o
plano de controle (criacao de processos, threads, exclusao mutua e IPC) e
POSIX. Um modo `--unsafe` desliga a exclusao mutua do corredor e expoe a
condicao de corrida como colisoes logicas, permitindo comparar problema e
solucao.

## Equipe

| Integrante | Responsabilidade |
| --- | --- |
| Luiz Felipe Souza da Silva | Concepcao e desenvolvimento do projeto (junto com Elian), base do codigo e repositorio GitHub; mantenedor do pacote |
| Elian Lima Pereira | Concepcao e desenvolvimento do projeto (junto com Luiz) |
| Diogo de Mello Silva | Revisao tecnica e correcoes |

Projeto academico, sem licenca de codigo aberto.

## Arquitetura

```text
                         operador (terminal)     outro terminal
                               | stdin                | echo > FIFO
                               v                      v
                   +------------------------------------------+
                   | fleet_supervisor (processo pai)          |
                   |  poll(stdin, FIFO) -> comandos           |
                   |  detector de colisoes | thread de logs   |
                   +------------------------------------------+
           fork+exec |     mq_send (1 fila POSIX por AMR)  ^ pipe (LogEvent)
        +------------+-------------+--------------+        |
        v            v             v              v        |
  turtlesim_node  amr_agent 1   amr_agent 2   amr_agent 3 -+
                  3 pthreads cada: spin ROS | comandos (produtor) | controle (consumidor)

  Memoria compartilhada /fleet_posix_state_<pid> (shm_open + mmap):
    pthread_mutex robusto e PROCESS_SHARED + estado da frota
    sem_t corridor (pshared=1, valor 1) = exclusao mutua no corredor
```

## Mecanismos de concorrencia, sincronizacao e IPC

| Conceito da disciplina | Onde e como e usado |
| --- | --- |
| Processos | `supervisor.cpp`: `fork` + `execl` do Turtlesim e de 3 agentes; `waitpid` (com `WNOHANG` e prazo), `_exit` no filho |
| Threads POSIX | `amr_agent.cpp`: `pthread_create`/`pthread_join` (spin ROS, comandos, controle); thread de logs no supervisor |
| Mutex | mutex `PTHREAD_PROCESS_SHARED` + `PTHREAD_MUTEX_ROBUST` em `FleetState`; mutexes locais de pose, movimento e buffer |
| Variavel de condicao | `pthread_cond_timedwait` (relogio monotonico): controle aguarda nova pose sem espera ocupada |
| Semaforos POSIX | `sem_init(pshared=1)` para o corredor (`sem_clockwait`/`sem_post`); par `free_slots`/`used_slots` no buffer |
| Produtor-consumidor | buffer circular limitado (4 missoes) entre a thread de comandos e a thread de controle |
| Secao critica / corrida | travessia do corredor protegida no modo seguro; `--unsafe` demonstra a condicao de corrida |
| Pipe | `pipe`: agentes enviam `LogEvent` (< `PIPE_BUF`, escrita atomica) ao supervisor |
| Named pipe (FIFO) | `mkfifo /tmp/fleet_posix_ctl_<pid>`: comandos de outro terminal, multiplexados com stdin via `poll` |
| Filas de mensagens POSIX | `mq_open`/`mq_send`/`mq_receive`: uma fila por AMR, comandos de controle com prioridade maior |
| Memoria compartilhada | `shm_open` + `ftruncate` + `mmap`: pose, missao e estado de toda a frota |
| Sinais | `sigaction` para `SIGINT`/`SIGTERM`; `kill(SIGTERM/SIGKILL)` no shutdown escalonado |

Detalhes de funcionamento e limites do modelo:
[`src/fleet_posix/README.md`](src/fleet_posix/README.md).

## Pre-requisitos

Ubuntu 24.04 (nativo ou WSL 2) com ROS 2 Jazzy. Em uma maquina sem ROS:

```bash
# Repositorio ROS 2: https://docs.ros.org/en/jazzy/Installation/Ubuntu-Install-Debs.html
sudo apt install ros-jazzy-ros-base ros-jazzy-turtlesim ros-jazzy-ament-cmake \
  ros-jazzy-ament-uncrustify python3-colcon-common-extensions build-essential
```

O codigo usa IPC POSIX do Linux: nao compila no Windows nativo. Para a
demonstracao visual no WSL e necessario WSLg (`DISPLAY` ou `WAYLAND_DISPLAY`).

## Como executar

Na raiz do repositorio (nao e preciso `source` antes do `make`):

```bash
make verify          # ambiente -> build -> estilo -> testes, sem abrir janela
make demo            # demo visual segura: tres missoes, zero colisoes
make demo-unsafe     # demo visual insegura: colisoes logicas esperadas
make run             # operacao online (comandos abaixo)
make simulate        # comparacao segura/insegura sem janela, com resumo
```

Comandos da operacao online, no terminal ou pelo FIFO impresso na inicializacao:

```text
enviar <amr> <x> <y>   pausar <amr>   retomar <amr>   status   sair
```

```bash
# Exemplo em outro terminal (substitua <pid> pelo PID mostrado pelo supervisor)
echo "enviar 1 9.0 5.15" > /tmp/fleet_posix_ctl_<pid>
```

Equivalente manual, sem Make:

```bash
source /opt/ros/jazzy/setup.bash
colcon build --symlink-install
source install/setup.bash
ros2 run fleet_posix fleet_supervisor --demo            # ou --demo --unsafe, ou sem opcoes
```

## Testes

`make test` executa duas entradas CTest, sem interface grafica: um teste C++ de
recuperacao do mutex robusto apos morte do processo dono e 12 cenarios de
integracao em Python (demos segura/insegura, FIFO, sinais, buffer cheio,
corredor ocupado com dono pausado, falhas parciais de IPC, morte de agente).
Cada cenario confere que nao restam processos nem objetos IPC
(`/dev/shm`, `/dev/mqueue`, FIFO em `/tmp`).

## Automacao Make

No PowerShell, os mesmos alvos funcionam com `wsl -d Ubuntu -- make verify`.
`make help` lista todos os alvos; `make status` mostra o andamento.

### Referencia dos alvos Make

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

#### Configuracao

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

#### Logs e diagnostico

- `log/automation/test.log`: saida da ultima suite executada por Make.
- `log/automation/demo-safe.log` e `demo-unsafe.log`: ultima comparacao offscreen.
- `build/fleet_posix/test_logs/`: logs individuais dos cenarios de integracao.
- `make ipc`: inspecao de processos e memoria/filas POSIX apos a execucao.

`rosdep` e `reportlab` sao opcionais: o primeiro so e usado por `make deps`;
o segundo apenas para regenerar os slides (o PDF pronto esta no repositorio).

## Andamento do projeto

<!-- andamento:inicio -->
Referencia validada em 2026-11-21. O quadro descreve o projeto; a existencia de
artefatos locais nao comprova que o codigo atual passou nas verificacoes.

| Etapa | Situacao | Evidencia ou proximo passo |
| --- | --- | --- |
| MVP e APIs POSIX | Implementado | Tres AMRs, processos, pthreads, sincronizacao e IPC explicitos. |
| Build e estilo C++ | Validados | GCC 13, C++17, -Werror e ament_uncrustify. |
| Testes sem janela | Validados | 12 cenarios Python + teste C++ de mutex robusto; use make test. |
| Comparacao segura/insegura | Validada | 3 missoes por modo; 0 colisoes no seguro, 2 no inseguro na referencia. |
| WSLg e operacao online | Validados | Demo visual e shutdown com pausa/corredor ocupado exercitados. |
| Automacao Make | Validada | make -j4 verify e make simulate aprovados; propagacao de falhas e limpeza conferidas. |
| Apresentacao | Pronta | Seis paginas em docs/FleetPOSIX_apresentacao.pdf. |
| Dependencias via rosdep | Pendente no ambiente | Instalar/inicializar rosdep e executar make deps. |
| Dados da equipe e licenca | Concluido | Equipe e divisao de trabalho na secao Equipe; sem licenca (Proprietary no package.xml). |
<!-- andamento:fim -->

Para acompanhar outra copia ou apos alterar os fontes, execute `make status`
e depois `make verify`. O quadro de referencia deve ser atualizado apenas
depois de repetir as validacoes relevantes.

A documentacao funcional, os comandos manuais equivalentes e o mapeamento
para a disciplina estao em [`src/fleet_posix/README.md`](src/fleet_posix/README.md).

## Material de entrega

- Apresentacao de seis paginas: [`docs/FleetPOSIX_apresentacao.pdf`](docs/FleetPOSIX_apresentacao.pdf).
- Gerador editavel dos slides: [`docs/generate_slides.py`](docs/generate_slides.py).
- Mantenedor no `package.xml`: Luiz Felipe Souza da Silva (luix.felipe008@gmail.com).
- Integrantes e divisao de trabalho: secao [Equipe](#equipe).
- Sem licenca de codigo aberto (`Proprietary` no `package.xml`).

Os slides incluem os resultados de `make verify`, o andamento e as pendencias,
e o roteiro Make para conferir o ambiente, testar e executar as simulacoes.

Para regenerar a apresentacao, execute `python docs/generate_slides.py` em um
ambiente com `reportlab` instalado. Essa dependencia e exclusiva do material
de apresentacao; nao e necessaria para compilar ou executar o projeto.
