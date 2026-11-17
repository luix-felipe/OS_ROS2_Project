# FleetPOSIX Workspace

Workspace ROS 2 Jazzy do projeto academico FleetPOSIX: uma central POSIX
controla tres AMRs independentes, representados por tartarugas, em uma regiao
critica compartilhada.

O ROS 2/Turtlesim e usado para pose, movimento e visualizacao. A orquestracao
usa explicitamente `fork`, `exec`, `waitpid`, pthreads, mutexes, variavel de
condicao, semaforos, pipe, filas POSIX, memoria compartilhada e sinais.

## Executar

```bash
cd /home/lfelipe/fleet_posix_ws
source /opt/ros/jazzy/setup.bash
colcon build --symlink-install
source install/setup.bash
ros2 run fleet_posix fleet_supervisor --demo
```

Comparacao com a condicao de corrida:

```bash
ros2 run fleet_posix fleet_supervisor --demo --unsafe
```

Para operacao online pelo terminal:

```bash
ros2 run fleet_posix fleet_supervisor
```

A documentacao completa esta em
[`src/fleet_posix/README.md`](src/fleet_posix/README.md).
