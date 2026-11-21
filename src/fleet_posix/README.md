# FleetPOSIX

MVP academico que representa tres AMRs independentes em uma fabrica. O ROS 2 e
o Turtlesim fornecem somente visualizacao, pose e movimento; o plano de controle
e implementado explicitamente com processos, threads e IPC POSIX.

## Funcionalidade

- o supervisor cria Turtlesim e tres agentes com `fork()` + `exec()`;
- cada agente cria sua propria tartaruga e possui threads de ROS, comandos e controle;
- o operador envia missoes online por filas de mensagens POSIX;
- a frota publica estado em memoria compartilhada;
- um semaforo compartilhado permite apenas um AMR no corredor central;
- mutexes e variavel de condicao protegem pose e estado;
- um pipe anonimo agrega logs de todos os agentes;
- um named pipe (FIFO) aceita comandos de outro terminal na operacao online;
- o supervisor encerra e recolhe os filhos com sinais e `waitpid()`.

O Turtlesim e uma abstracao 2D. Nao ha pretensao de simular dinamica, sensores,
SLAM ou planejamento de trajetoria de um AMR real.

## Build

Na raiz do workspace, o `Makefile` oferece `make env`, `make build`, `make test`
e `make verify`. Cada alvo carrega seu ambiente ROS automaticamente. Use
`make help` para todas as tarefas e `make status` para o andamento documentado
e os artefatos locais. O guia de automacoes esta no README da raiz. Os comandos
manuais equivalentes permanecem abaixo.

```bash
# Execute na raiz do repositorio, em um terminal Ubuntu/WSL.
source /opt/ros/jazzy/setup.bash
rosdep check --from-paths src --ignore-src --rosdistro jazzy
colcon build --symlink-install
source install/setup.bash
```

## Demonstracao automatica

```bash
ros2 run fleet_posix fleet_supervisor --demo
```

Os tres AMRs recebem destinos simultaneamente. No modo seguro, eles atravessam
o centro um por vez. Para demonstrar a condicao de corrida:

```bash
ros2 run fleet_posix fleet_supervisor --demo --unsafe
```

As opcoes podem ser fornecidas em qualquer ordem. `--demo` retorna codigo zero
somente quando os tres AMRs concluem as missoes dentro do prazo e a contagem de
colisoes corresponde ao modo: zero no seguro e pelo menos uma no inseguro.
Falha de inicializacao, morte de um filho ou timeout produzem codigo diferente
de zero, apos o encerramento e a limpeza.

O script `bash src/fleet_posix/scripts/demo.sh [--unsafe]` funciona a partir dos
fontes. Depois do build, `ros2 run fleet_posix demo.sh [--unsafe]` tambem funciona.
Nao e preciso usar o caminho da maquina original.

## Operacao online

```bash
ros2 run fleet_posix fleet_supervisor
```

Comandos disponiveis:

```text
enviar <amr> <x> <y>
pausar <amr>
retomar <amr>
status
sair
```

Exemplo:

```text
enviar 1 9.0 5.15
enviar 2 5.5 9.0
enviar 3 2.0 5.85
status
pausar 1
retomar 1
sair
```

Use coordenadas entre 0.5 e 10.5, fora da regiao critica (circulo de raio 2.35
em torno de (5.5, 5.5)); destinos dentro dela sao recusados, pois um AMR parado
ali sem possuir o semaforo quebraria a exclusao mutua. `Ctrl+C` solicita
encerramento geral.

### Comandos por named pipe (FIFO)

Na operacao online o supervisor cria `/tmp/fleet_posix_ctl_<pid>` com `mkfifo`
e o multiplexa com o terminal via `poll()`. Em outro terminal:

```bash
echo "enviar 1 9.0 5.15" > /tmp/fleet_posix_ctl_<pid>
echo "status"            > /tmp/fleet_posix_ctl_<pid>
```

O caminho exato e impresso na inicializacao. O supervisor mantem um escritor
interno aberto, de modo que o fechamento de cada `echo` nao gera EOF continuo.
O FIFO e removido com `unlink` no encerramento.

O detector acompanha as poses continuamente, mesmo sem novos comandos no
terminal. No modo online inseguro (`--unsafe`), ele tambem contabiliza colisoes.
O buffer local comporta quatro missoes pendentes por agente. Quando cheio,
novas missoes sao recusadas com um aviso; pausa, retomada e shutdown continuam
sendo recebidos. A fila POSIX tambem recusa envios quando cheia, sem bloquear
o terminal. Comandos de controle possuem prioridade sobre missoes pendentes
na fila POSIX.

O shutdown cancela a missao ativa e as pendentes, acorda o consumidor e a
espera por pose, e libera o corredor ocupado. Uma pausa dentro do corredor
conserva sua reserva ate retomada ou shutdown. O supervisor aguarda os filhos
com prazo limitado e usa sinais se um processo nao responder.

A exclusao mutua protege a travessia da regiao central nas rotas de referencia.
Destinos online arbitrarios exigem cuidado do operador: nao ha planejamento
de rotas nem desvio de AMRs parados fora do corredor. Destinos dentro da regiao
central sao recusados; evite tambem destinos coincidentes com posicoes de espera
de outros AMRs. A espera pelo corredor usa tentativas com prazo
(`sem_clockwait` em `CLOCK_MONOTONIC`), sem ordem FIFO garantida entre AMRs.

## Testes sem interface grafica

```bash
source /opt/ros/jazzy/setup.bash
colcon build --symlink-install --event-handlers console_direct+
source install/setup.bash
ament_uncrustify src/fleet_posix/src src/fleet_posix/include src/fleet_posix/test
colcon test --event-handlers console_direct+ --return-code-on-test-failure
colcon test-result --verbose
```

Sao duas entradas CTest: um teste C++ de recuperacao do mutex compartilhado
apos morte do proprietario e uma suite Python com doze cenarios de integracao:

- demonstracoes segura e insegura;
- deteccao de colisoes online sem novas entradas no terminal;
- comandos pelo FIFO e recusa de destino dentro da regiao critica;
- shutdown com proprietario pausado e outros agentes aguardando o corredor;
- retomada e shutdown com buffer cheio;
- `SIGINT` no grupo de processos com entrada parcial no terminal;
- `SIGTERM` com missao pausada;
- falhas parciais de IPC por limite de descritores;
- falha de inicializacao do Turtlesim;
- morte inesperada de um agente;
- argumentos invalidos e execucao do script pelos fontes e pela instalacao.

Os testes usam apenas a biblioteca padrao Python, os executaveis compilados e
Turtlesim offscreen. Um dominio ROS separado evita interferir na demonstracao
padrao; defina `FLEET_TEST_ROS_DOMAIN_ID` para escolher outro dominio. Cada
execucao verifica a ausencia de processos e IPCs proprios ao terminar. Logs:
`build/fleet_posix/test_logs/`. Os testes nao executam a interface grafica.

O mutex da frota e compartilhado e robusto. `EOWNERDEAD` permite recuperar o
lock para encerrar a frota e limpar recursos se um processo morrer durante a
secao critica; isso nao retoma automaticamente a missao interrompida.

Para verificar dependencias separadamente, use `rosdep check --from-paths src
--ignore-src --rosdistro jazzy`. Esse comando exige `rosdep` instalado e sua
base inicializada. A compilacao verifica as dependencias CMake do pacote.

## Mapeamento para a disciplina

| Conceito | Evidencia no projeto |
| --- | --- |
| Processos | `fork`, `exec`, `waitpid`, `_exit` e `exit` no supervisor/agentes |
| Threads | `pthread_create` e `pthread_join` em cada agente e no logger |
| Mutex | estado compartilhado, pose e buffer de comandos |
| Condicao | thread de controle aguarda nova pose sem espera ocupada |
| Semaforos | buffer produtor-consumidor e corredor compartilhado |
| Pipe | eventos dos agentes para o supervisor (`pipe`, escrita atomica < `PIPE_BUF`) |
| Named pipe (FIFO) | comandos externos (`mkfifo`, `open`, `poll`, `unlink`) |
| Filas POSIX | comandos online do operador para cada AMR |
| Memoria compartilhada | pose, missao e estado de toda a frota |
| Sinais | emergencia por `SIGINT` e encerramento do Turtlesim |

## Escopo deliberadamente excluido

- Gazebo, Nav2, SLAM e URDF;
- sensores e hardware reais;
- desvio de obstaculos e planejamento de rotas;
- persistencia, rede e interface grafica propria.

Mantenedor do pacote: Luiz Felipe Souza da Silva (luix.felipe008@gmail.com).
Equipe: Luiz Felipe Souza da Silva, Elian Lima Pereira e Diogo de Mello Silva.
Projeto academico sem licenca de codigo aberto (`Proprietary` no `package.xml`).
