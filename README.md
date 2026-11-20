# FleetPOSIX Workspace

Workspace ROS 2 Jazzy do projeto academico FleetPOSIX: uma central POSIX
controla tres AMRs independentes, representados por tartarugas, em uma regiao
critica compartilhada.

O ROS 2/Turtlesim e usado para pose, movimento e visualizacao. A orquestracao
usa explicitamente `fork`, `exec`, `waitpid`, pthreads, mutexes, variavel de
condicao, semaforos, pipe, filas POSIX, memoria compartilhada e sinais.

## Executar

```bash
# No terminal Ubuntu/WSL, abra a raiz deste repositorio.
WORKSPACE_ROOT="$(pwd)"
source /opt/ros/jazzy/setup.bash
colcon build --symlink-install --event-handlers console_direct+
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

## Verificar antes da apresentacao

```bash
source /opt/ros/jazzy/setup.bash
source install/setup.bash
ament_uncrustify src/fleet_posix/src src/fleet_posix/include src/fleet_posix/test
colcon test --event-handlers console_direct+ --return-code-on-test-failure
colcon test-result --verbose
```

A suite executa as demonstracoes e os testes de comandos, sinais, falhas e
limpeza com `QT_QPA_PLATFORM=offscreen`, sem abrir janelas. Os logs ficam em
`build/fleet_posix/test_logs/`. Execute build e testes no Ubuntu/WSL; os IPCs
POSIX do projeto nao sao suportados pelo compilador nativo do Windows.

O script `bash src/fleet_posix/scripts/demo.sh` descobre a raiz automaticamente.
Tambem pode ser chamado com `ros2 run fleet_posix demo.sh`, apos o build.
Os arquivos de texto usam LF para funcionar igualmente no Git e no WSL.

## Material de entrega

- Apresentacao de seis paginas: [`docs/FleetPOSIX_apresentacao.pdf`](docs/FleetPOSIX_apresentacao.pdf).
- Gerador editavel dos slides: [`docs/generate_slides.py`](docs/generate_slides.py).
- Mantenedor, email e licenca ainda precisam ser definidos pela equipe em `package.xml`.
- A equipe deve registrar os integrantes e sua divisao de trabalho antes da entrega.

Para regenerar a apresentacao, execute `python docs/generate_slides.py` em um
ambiente com `reportlab` instalado. Essa dependencia e exclusiva do material
de apresentacao; nao e necessaria para compilar ou executar o projeto.
