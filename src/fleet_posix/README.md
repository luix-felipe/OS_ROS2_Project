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
- um pipe agrega logs de todos os agentes;
- o supervisor encerra e recolhe os filhos com sinais e `waitpid()`.

O Turtlesim e uma abstracao 2D. Nao ha pretensao de simular dinamica, sensores,
SLAM ou planejamento de trajetoria de um AMR real.

## Build

```bash
cd /home/lfelipe/fleet_posix_ws
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
enviar 1 9.0 5.5
enviar 2 5.5 9.0
status
pausar 1
retomar 1
sair
```

Use coordenadas entre 0.5 e 10.5. `Ctrl+C` solicita encerramento geral.

## Mapeamento para a disciplina

| Conceito | Evidencia no projeto |
| --- | --- |
| Processos | `fork`, `exec`, `waitpid`, `_exit` e `exit` no supervisor/agentes |
| Threads | `pthread_create` e `pthread_join` em cada agente e no logger |
| Mutex | estado compartilhado, pose e buffer de comandos |
| Condicao | thread de controle aguarda nova pose sem espera ocupada |
| Semaforos | buffer produtor-consumidor e corredor compartilhado |
| Pipe | eventos dos agentes para o supervisor |
| Filas POSIX | comandos online do operador para cada AMR |
| Memoria compartilhada | pose, missao e estado de toda a frota |
| Sinais | emergencia por `SIGINT` e encerramento do Turtlesim |

## Escopo deliberadamente excluido

- Gazebo, Nav2, SLAM e URDF;
- sensores e hardware reais;
- desvio de obstaculos e planejamento de rotas;
- persistencia, rede e interface grafica propria.

Antes da entrega, substitua o mantenedor e a licenca no `package.xml` pelos
dados definidos pela equipe.
