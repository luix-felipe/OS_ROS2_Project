# AGENTS.md — FleetPOSIX

Este arquivo fornece contexto operacional e arquitetural para agentes de codigo
que trabalhem neste workspace. Leia-o antes de alterar codigo, executar o
Turtlesim ou ampliar o escopo.

## Objetivo do projeto

FleetPOSIX e um MVP academico para demonstrar diretamente os conceitos de
programacao concorrente, sincronizacao e IPC POSIX exigidos na disciplina.

O cenario representa tres AMRs independentes trabalhando em uma fabrica ou
almoxarifado. Um operador envia missoes online e uma regiao central compartilhada
precisa ser sincronizada para evitar colisoes.

O ROS 2 e o Turtlesim sao deliberadamente limitados ao plano de simulacao:

- criacao e visualizacao dos tres AMRs;
- publicacao de pose;
- recepcao de comandos de velocidade.

O plano de controle deve continuar explicitamente POSIX. Nao substitua as APIs
avaliadas por abstracoes internas do ROS 2, `std::thread`, `std::mutex` ou apenas
por um `MultiThreadedExecutor`.

## Ambiente de referencia

- Ubuntu 24.04 em WSL 2;
- ROS 2 Jazzy;
- Turtlesim 1.8.x;
- compilador GCC/G++ 13;
- C++17;
- `colcon`, `rosdep` e `ament_uncrustify`;
- WSLg com `DISPLAY=:0` para a demonstracao visual.

O caminho observado na maquina de origem e:

```text
/home/lfelipe/fleet_posix_ws
```

Esse caminho nao e parte do contrato do codigo. Em outra maquina, trate a raiz
obtida por `pwd` como `WORKSPACE_ROOT` e nao introduza novos caminhos absolutos.

## Estrutura relevante

```text
fleet_posix_ws/
├── AGENTS.md
├── README.md
├── Makefile
├── scripts/workspace.sh
├── .gitignore
└── src/
    └── fleet_posix/
        ├── CMakeLists.txt
        ├── package.xml
        ├── README.md
        ├── include/fleet_posix/common.hpp
        ├── src/supervisor.cpp
        ├── src/amr_agent.cpp
        ├── test/test_integration.py
        ├── test/test_shared_state.cpp
        └── scripts/demo.sh
```

Responsabilidades:

- `supervisor.cpp`: cria e remove IPC, executa `fork`/`exec`, recebe comandos do
  operador, agrega logs, detecta colisoes, envia shutdown e usa `waitpid`;
- `amr_agent.cpp`: implementa um processo ROS 2 por AMR e suas threads POSIX de
  spin, recepcao de comandos e controle;
- `common.hpp`: contrato binario compartilhado entre processos, constantes da
  regiao critica, comandos, estados e eventos;
- `README.md` da raiz: entrada rapida do workspace;
- `src/fleet_posix/README.md`: documentacao funcional e mapeamento da disciplina.

## Arquitetura atual

O supervisor deve criar todos os processos antes de iniciar a thread de log.
Isso evita chamar `fork()` depois que ROS 2 ou outras bibliotecas ja tiverem
criado threads.

```text
fleet_supervisor
├── fork + exec: turtlesim_node
├── fork + exec: amr_agent 1
├── fork + exec: amr_agent 2
├── fork + exec: amr_agent 3
└── pthread: agregador do pipe de logs

amr_agent N
├── pthread: rclcpp::spin
├── pthread: mq_receive e producao de comandos
└── pthread: consumo de comandos e controle do movimento
```

Cada agente cria sua propria tartaruga (`amr1`, `amr2` ou `amr3`) pelo servico
`/spawn`. O agente 1 remove a `turtle1` padrao quando possivel.

## Mapeamento obrigatorio das APIs

Preserve o uso explicito destes mecanismos:

| Requisito | Implementacao atual |
| --- | --- |
| Processos | `fork`, `execl`, `_exit`, `exit`, `waitpid` |
| Threads | `pthread_create`, `pthread_join` |
| Mutexes | estado compartilhado, pose e buffer local |
| Condicao | `pthread_cond_timedwait` para aguardar pose |
| Semaforos | buffer produtor-consumidor e corredor compartilhado |
| Pipe | `LogEvent` dos agentes para o supervisor |
| Named pipe (FIFO) | `/tmp/fleet_posix_ctl_<pid>`: comandos externos multiplexados com stdin via `poll` |
| Filas POSIX | uma fila de comandos por AMR |
| Memoria compartilhada | `FleetState` via `shm_open` e `mmap` |
| Sinais | `SIGINT`/`SIGTERM` e encerramento do Turtlesim |

O semaforo do corredor fica dentro da memoria compartilhada e usa
`sem_init(..., 1, 1)`, portanto e compartilhado entre processos. Os semaforos
do buffer de cada agente usam `pshared=0`, pois sincronizam threads locais.

O mutex de `FleetState` usa `PTHREAD_PROCESS_SHARED` e `PTHREAD_MUTEX_ROBUST`.
Use `lock_fleet_state()` para adquirir esse mutex, tratando `EOWNERDEAD` para
permitir o encerramento e a limpeza apos morte de um filho. A recuperacao do
mutex nao implica retomar uma missao interrompida.

O supervisor verifica colisoes tanto em `--demo` quanto no modo online. O
terminal usa `poll`/`read`, mantendo o monitoramento ativo sem nova entrada.
Filas POSIX e insercao no buffer local nao bloqueiam quando cheias: comandos
ou missoes sao recusados com aviso. Pausa, retomada e shutdown nao aguardam
espaco no buffer de missoes.

## IPC e ciclo de vida

Os nomes dos objetos POSIX incluem o PID do supervisor:

```text
/fleet_posix_state_<pid>
/fleet_posix_cmd_<pid>_1
/fleet_posix_cmd_<pid>_2
/fleet_posix_cmd_<pid>_3
```

Isso permite execucoes independentes e reduz conflitos com recursos antigos.
Toda saida normal deve:

1. enviar `kShutdown` aos tres agentes;
2. liberar eventuais esperas no semaforo;
3. recolher os agentes com `waitpid`;
4. terminar e recolher o Turtlesim;
5. fechar e remover filas com `mq_close`/`mq_unlink`;
6. destruir mutex e semaforo compartilhados;
7. executar `munmap` e `shm_unlink`.

O shutdown cancela a missao ativa e as pendentes. O agente acorda seu consumidor
com `sem_post` e sua espera por pose com `pthread_cond_broadcast`. A espera do
corredor usa `sem_timedwait`, verificando cancelamento entre tentativas.
O supervisor limita a espera pelos filhos e usa `SIGTERM`/`SIGKILL` como
contingencia. A limpeza de IPC tambem cobre falhas parciais de inicializacao.

Nao altere o layout de `Command`, `FleetState` ou `LogEvent` em apenas um dos
executaveis. Ambos incluem `common.hpp` justamente para manter o contrato
binario consistente.

`LogEvent` deve permanecer menor que `PIPE_BUF`, para que cada evento escrito
pelos diferentes agentes continue atomico no pipe compartilhado.

## Geometria e resolucao de colisao

O Turtlesim nao possui obstaculos fisicos. A regiao critica e logica e fica ao
redor de `(5.5, 5.5)`. Seus raios estao em `common.hpp`.

Rotas automaticas atuais:

- AMR 1: `(2.0, 5.15)` para `(9.0, 5.15)`;
- AMR 2: `(5.5, 2.0)` para `(5.5, 9.0)`;
- AMR 3: `(9.0, 5.85)` para `(2.0, 5.85)`.

Historico importante: a primeira versao colocava AMR 1 e AMR 3 na mesma linha,
trocando exatamente seus pontos inicial e final. Isso produzia uma colisao fora
da regiao critica mesmo no modo seguro. A solucao foi separar as duas faixas
horizontais, mantendo as intersecoes apenas dentro da zona protegida.

Nao volte a usar destinos que coincidam com a posicao de espera de outro AMR.
Se as rotas ou raios forem alterados, valide novamente os dois modos:

- seguro: tres missoes concluidas e zero colisoes;
- inseguro: tres missoes concluidas e pelo menos uma colisao logica.

## Escopo do MVP

Faz parte do MVP:

- tres AMRs;
- destinos cartesianos simples;
- controle proporcional de orientacao e velocidade;
- uma regiao critica;
- operacao online no terminal;
- comparacao segura versus insegura;
- encerramento e limpeza deterministas.

Nao faz parte do MVP, salvo solicitacao explicita:

- Gazebo, Nav2, SLAM, URDF ou sensores realistas;
- planejamento de trajetoria ou desvio de obstaculos;
- comunicacao em rede, banco de dados ou interface grafica propria;
- integracao com o workspace DGBOT;
- simulacao de safety industrial.

Turtlesim e uma abstracao visual 2D. Nao descreva o MVP como simulacao fisica
realista de um AMR.

## Build e verificacoes

O `Makefile` da raiz oferece ajuda em portugues com `make help` e consulta o
andamento documentado no README com `make status`. As tarefas sao implementadas
em `scripts/workspace.sh`, que descobre a raiz e carrega ROS/overlay conforme
necessario. Caminhos com espacos devem permanecer protegidos por aspas.

Fluxo automatizado no Ubuntu/WSL:

```bash
make env
make verify
make simulate
make ipc
```

`make verify` serializa ambiente, build, estilo e testes, incluindo as duas
demos offscreen da suite. `make simulate` compara os modos separadamente e
salva logs em `log/automation/`. `make demo`, `make demo-unsafe`, `make run` e
`make run-unsafe` sao alvos graficos explicitos e nao entram em `verify`.
`make deps` executa rosdep separadamente e falha quando a ferramenta/base esta
ausente. Nao esconda essa pendencia em um resultado positivo de dependencias.

`make clean` remove apenas `build/`, `install/` e `log/` desta raiz, e recusa
links simbolicos nesses destinos. Nao execute limpeza automaticamente em
build/test/verify. `make ipc` e somente leitura e pode listar outras execucoes.

Os comandos manuais equivalentes continuam validos. Na raiz do workspace:

```bash
source /opt/ros/jazzy/setup.bash
rosdep check --from-paths src --ignore-src --rosdistro jazzy
colcon build --symlink-install --event-handlers console_direct+
ament_uncrustify src/fleet_posix/src src/fleet_posix/include
colcon test --event-handlers console_direct+ --return-code-on-test-failure
colcon test-result --verbose
```

O build usa `-Wall -Wextra -Wpedantic -Werror`. Nao remova `-Werror` apenas para
ocultar um problema; corrija o aviso.

Validacao sem abrir janela:

```bash
source install/setup.bash
QT_QPA_PLATFORM=offscreen ros2 run fleet_posix fleet_supervisor --demo
QT_QPA_PLATFORM=offscreen ros2 run fleet_posix fleet_supervisor --demo --unsafe
```

Validacao visual no WSLg:

```bash
source install/setup.bash
ros2 run fleet_posix fleet_supervisor --demo
```

Nao use a execucao grafica como teste de sintaxe. Compile primeiro e use o modo
offscreen para diagnosticos automatizados.

## Operacao online

```bash
source /opt/ros/jazzy/setup.bash
source install/setup.bash
ros2 run fleet_posix fleet_supervisor
```

Comandos:

```text
enviar <amr> <x> <y>
pausar <amr>
retomar <amr>
status
sair
```

As coordenadas aceitas ficam entre `0.5` e `10.5`.

## Resultado de referencia

Validacao realizada em 2026-11-17:

- dependencias do pacote satisfeitas;
- compilacao concluida sem avisos;
- `ament_uncrustify` sem divergencias;
- demonstracao offscreen e visual executadas com sucesso;
- modo seguro: 3 missoes, 0 colisoes;
- modo inseguro: 3 missoes, 2 colisoes logicas;
- agentes finalizados com `pthread_join` e `waitpid`;
- nenhum objeto `fleet_posix_*` residual em `/dev/shm` ou `/dev/mqueue`.

Ao fazer mudancas funcionais, atualize esta evidencia apenas depois de repetir
as validacoes relevantes.

Revalidacao realizada em 2026-11-20, apos as correcoes de ciclo de vida:

- Ubuntu 24.04, ROS 2 Jazzy, Turtlesim 1.8.4 e GCC 13.3;
- compilacao com `-Wall -Wextra -Wpedantic -Werror` concluida;
- `ament_uncrustify` sem divergencias nos fontes, headers e teste C++;
- duas entradas CTest aprovadas: 11 cenarios Python de integracao e um teste
  C++ de recuperacao do mutex apos morte do proprietario;
- modo seguro offscreen: 3 missoes concluidas e 0 colisoes;
- modo inseguro offscreen: 3 missoes concluidas e 2 colisoes logicas;
- modo online inseguro contabiliza colisoes sem exigir nova entrada no terminal;
- shutdown com proprietario pausado, agentes esperando e buffer cheio aprovado;
- `SIGINT`, `SIGTERM`, morte de agente e falha do Turtlesim exercitados;
- falhas por limite de descritores nao deixam memoria nem filas residuais;
- execucao grafica no WSLg concluida: 3 missoes e 0 colisoes;
- nenhum processo da frota nem objeto `fleet_posix_*` residual ao terminar;
- script testado pelos fontes e pela instalacao, a partir de outro diretorio;
- apresentacao de seis paginas em `docs/FleetPOSIX_apresentacao.pdf`;
- `rosdep check` nao repetido: `rosdep` nao esta instalado neste ambiente;
  as dependencias CMake foram encontradas durante a compilacao.

Automacoes Make validadas em 2026-11-20:

- ajuda, andamento, diagnostico e leitura de resultados executados no WSL;
- `make -j4 verify` compilou uma vez e aprovou estilo e os 12 cenarios existentes;
- `make -j4 simulate ROS_DOMAIN_ID=171`: tres missoes por modo, zero colisoes no
  seguro e duas no inseguro, com logs separados;
- `make demo ROS_DOMAIN_ID=171` executado no WSLg: tres missoes e zero colisoes;
- inspecao apos as simulacoes sem processos ou IPCs residuais;
- sequencia, interrupcao em falhas de build/estilo/teste/simulacao e recusa de
  GUI sem display conferidas em uma copia temporaria com ferramentas simuladas;
- limpeza conferida somente nessa copia temporaria: fontes preservados e
  destinos simbolicos recusados; os artefatos reais nao foram removidos;
- `rosdep` e `reportlab` ausentes no Ubuntu: avisos em `make env`;
  `make deps` e regeneracao de slides continuam dependendo dessas ferramentas.

Revisao realizada em 2026-11-21 (ROS 2 Jazzy via RoboStack, GCC 13, uncrustify 0.83):

- `demo.sh` respeita `ROS_SETUP`/`ROS_DISTRO` em vez de `/opt/ros/jazzy` fixo;
- filhos recebem `/dev/null` como stdin; o terminal pertence ao supervisor;
- `print_status` e `detect_collisions` copiam o estado sob o mutex e imprimem fora;
- esperas temporizadas do agente usam `CLOCK_MONOTONIC` (`pthread_condattr_setclock`,
  `sem_clockwait`);
- destinos online dentro da regiao critica sao recusados;
- novo canal de comandos por named pipe (FIFO) no modo online, com teste;
- 12 cenarios Python + teste C++ aprovados, inclusive com ASan/UBSan (3 rodadas);
- `make simulate`: 0 colisoes no seguro e 2 no inseguro.

## Regras para alteracoes

1. Preserve o `dgbot_ws`; este e um workspace separado.
2. Prefira caminhos relativos e descoberta via ament a caminhos absolutos.
3. Crie processos antes de iniciar threads ou inicializar runtimes complexos.
4. Nao use `sleep` como mecanismo de sincronizacao; sleeps curtos so podem
   representar tempo de simulacao ou polling com limite.
5. Proteja toda escrita concorrente em `FleetState` com seu mutex compartilhado.
6. Nao execute callbacks ROS segurando o mutex compartilhado.
7. Garanta que qualquer caminho de shutdown libere o corredor se necessario.
8. Novos IPCs devem ter nome unico e rotina explicita de limpeza.
9. Mantenha o modo `--demo` deterministico para a apresentacao.
10. Nao aumente o escopo antes de o MVP continuar reproduzivel.

## Pendencias conhecidas

- instalar/inicializar `rosdep` e repetir sua verificacao separada de dependencias;

Essas pendencias nao invalidam a demonstracao atual, mas devem ser consideradas
antes da entrega final.
