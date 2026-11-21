#!/usr/bin/env bash
# Implementacao dos alvos Make: mensagens em portugues e erros propagados.
set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${WORKSPACE_ROOT}"
TASK="${1:-help}"
ROS_DISTRO="${ROS_DISTRO:-jazzy}"
ROS_SETUP="${ROS_SETUP:-/opt/ros/${ROS_DISTRO}/setup.bash}"
PYTHON="${PYTHON:-python3}"
JOBS="${JOBS:-2}"
DEMO_TIMEOUT="${DEMO_TIMEOUT:-45s}"
AUTOMATION_LOG_DIR="${WORKSPACE_ROOT}/log/automation"

fail() {
  printf '[ERRO] %s\n' "$*" >&2
  exit 1
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || fail "Comando '$1' ausente no Ubuntu/WSL. Consulte make env."
}

load_ros() {
  [[ "$(uname -s)" == Linux ]] || fail 'Execute esta tarefa no Ubuntu/WSL; o projeto usa IPC POSIX Linux.'
  [[ -f "${ROS_SETUP}" ]] || fail "ROS nao encontrado em ${ROS_SETUP}. Confira ROS_SETUP e ROS_DISTRO."
  # Os scripts de ambiente ROS podem consultar variaveis ainda nao definidas.
  source "${ROS_SETUP}"
}

load_overlay() {
  load_ros
  [[ -f install/setup.bash ]] || fail 'Overlay ausente. Execute make build primeiro.'
  source install/setup.bash
}

show_help() {
  cat <<'HELP'
FleetPOSIX - automacoes do workspace (Ubuntu 24.04 / WSL 2, ROS 2 Jazzy)

Comece aqui:
  make                 Mostra esta ajuda, sem compilar nem abrir janelas.
  make status          Explica o andamento e mostra artefatos/resultados locais.
  make env             Confere ferramentas e pacotes ROS; avisa sobre opcionais.
  make deps            Confere dependencias com rosdep (exige ferramenta e base).

Desenvolvimento e verificacao:
  make build           Compila com colcon, C++17, -Werror e testes habilitados.
  make lint            Confere o estilo C++ com ament_uncrustify, sem modificar.
  make format          Aplica a formatacao C++ (modifica fontes).
  make test            Compila e executa os testes sem janela; imprime resultados.
  make report          Mostra os ultimos resultados registrados, sem repetir testes.
  make verify          Executa env -> build -> lint -> test; para na primeira falha.

Simulacoes:
  make simulate        Compila e compara seguro/inseguro offscreen, salvando logs.
  make demo            Compila e abre a demo segura no WSLg (3 missoes, 0 colisoes).
  make demo-unsafe     Compila e abre a demo insegura (colisoes logicas esperadas).
  make run             Compila e abre operacao online segura no terminal + WSLg.
  make run-unsafe      Operacao online sem exclusao mutua do corredor.

Inspecao e entrega:
  make ipc             Lista processos e objetos fleet_posix_*; nao remove nada.
  make slides          Regenera os 6 slides em PDF (Python com reportlab).
  make clean           Remove SOMENTE build/, install/ e log/ deste workspace.

Fluxo sugerido: make status -> make env -> make verify -> make simulate -> make demo
Comandos online: enviar <amr> <x> <y> | pausar <amr> | retomar <amr> | status | sair
Ctrl+C solicita shutdown. Turtlesim e uma abstracao visual 2D.

Opcoes: JOBS=2 ROS_DOMAIN_ID=0 DEMO_TIMEOUT=45s PYTHON=python3
        ROS_DISTRO=jazzy ROS_SETUP=/opt/ros/jazzy/setup.bash
Exemplo: make simulate ROS_DOMAIN_ID=171 JOBS=2
Logs: log/automation/ e build/fleet_posix/test_logs/ (ultima execucao).
Nao e preciso executar source antes de make; cada tarefa carrega seu ambiente.
HELP
}

check_environment() {
  printf '\n[AMBIENTE] Conferindo o workspace: %s\n' "${WORKSPACE_ROOT}"
  load_ros
  local missing=0 tool package directory
  for tool in make g++ cmake "${PYTHON}" colcon ros2 ament_uncrustify timeout; do
    if command -v "${tool}" >/dev/null 2>&1; then
      printf '[OK] %-20s %s\n' "${tool}" "$(command -v "${tool}")"
    else
      printf '[FALTA] Ferramenta: %s\n' "${tool}"
      missing=1
    fi
  done
  if command -v ros2 >/dev/null 2>&1; then
    for package in ament_cmake ament_cmake_test ament_index_cpp geometry_msgs rclcpp turtlesim; do
      if ros2 pkg prefix "${package}" >/dev/null 2>&1; then
        printf '[OK] Pacote ROS: %s\n' "${package}"
      else
        printf '[FALTA] Pacote ROS: %s\n' "${package}"
        missing=1
      fi
    done
  fi
  for directory in /dev/shm /dev/mqueue; do
    if [[ -d "${directory}" && -r "${directory}" && -w "${directory}" && -x "${directory}" ]]; then
      printf '[OK] Diretorio de IPC acessivel: %s\n' "${directory}"
    else
      printf '[FALTA] Diretorio de IPC sem acesso de leitura/escrita: %s\n' "${directory}"
      missing=1
    fi
  done
  printf '[INFO] ROS_DISTRO=%s | ROS_DOMAIN_ID=%s | JOBS=%s\n' \
    "${ROS_DISTRO}" "${ROS_DOMAIN_ID:-0}" "${JOBS}"
  command -v g++ >/dev/null 2>&1 && g++ --version | head -n 1
  if command -v rosdep >/dev/null 2>&1; then
    printf '[INFO] rosdep encontrado; execute make deps para conferir a base e as dependencias.\n'
  else
    printf '[AVISO] rosdep ausente: make deps ficara pendente; build/test usam os pacotes ROS instalados.\n'
  fi
  if command -v "${PYTHON}" >/dev/null 2>&1 && "${PYTHON}" -c 'import reportlab' >/dev/null 2>&1; then
    printf '[OK] reportlab disponivel para make slides.\n'
  else
    printf '[AVISO] reportlab ausente neste Python; opcional, exclusivo de make slides.\n'
  fi
  if [[ -n "${DISPLAY:-}" || -n "${WAYLAND_DISPLAY:-}" ]]; then
    printf '[INFO] Sessao grafica anunciada; make demo/run verificam a execucao no WSLg.\n'
  else
    printf '[AVISO] Sem DISPLAY/WAYLAND_DISPLAY. Use make simulate e make test, sem janela.\n'
  fi
  [[ "${missing}" == 0 ]] || fail 'Ambiente incompleto. Instale as ferramentas/pacotes marcados antes de continuar.'
  printf '[OK] Pre-requisitos de build e testes encontrados; isso nao substitui make deps.\n'
}

build_workspace() {
  printf '\n[BUILD] Compilando C++17 com -Werror e BUILD_TESTING=ON.\n'
  load_ros
  require_command colcon
  [[ "${JOBS}" =~ ^[1-9][0-9]*$ ]] || fail 'JOBS deve ser um inteiro positivo.'
  # O build tem seu proprio limite JOBS; nao herda o jobserver do Make externo.
  unset MAKEFLAGS MFLAGS MAKELEVEL
  export CMAKE_BUILD_PARALLEL_LEVEL="${JOBS}"
  colcon build --symlink-install --event-handlers console_direct+ \
    --parallel-workers "${JOBS}" --cmake-args -DBUILD_TESTING=ON
}

check_style() {
  load_ros
  require_command ament_uncrustify
  local result=0
  if [[ "${TASK}" == format ]]; then
    printf '\n[FORMATACAO] Aplicando estilo aos fontes e testes C++; confira git diff depois.\n'
    # A ferramenta retorna 1 quando precisou reformatar; uma nova checagem valida o resultado.
    ament_uncrustify --reformat src/fleet_posix/src src/fleet_posix/include src/fleet_posix/test || result=$?
    [[ "${result}" -le 1 ]] || return "${result}"
  else
    printf '\n[ESTILO] Conferindo C++ sem alterar arquivos.\n'
  fi
  ament_uncrustify src/fleet_posix/src src/fleet_posix/include src/fleet_posix/test
}

require_test_results() {
  local name
  for name in fleet_posix_shared_state fleet_posix_integration; do
    [[ -f "build/fleet_posix/test_results/fleet_posix/${name}.xml" ]] || \
      fail "Resultado ${name}.xml ausente. Execute make test; artefato ausente nao significa teste aprovado."
  done
}

run_tests() {
  printf '\n[TESTES] Executando mutex robusto e 12 cenarios de integracao offscreen.\n'
  load_overlay
  require_command colcon
  mkdir -p "${AUTOMATION_LOG_DIR}"
  local result=0 report_result=0
  colcon test --event-handlers console_direct+ --return-code-on-test-failure \
    2>&1 | tee "${AUTOMATION_LOG_DIR}/test.log" || result=$?
  colcon test-result --verbose 2>&1 | tee -a "${AUTOMATION_LOG_DIR}/test.log" || report_result=$?
  [[ "${result}" == 0 ]] || return "${result}"
  [[ "${report_result}" == 0 ]] || return "${report_result}"
  require_test_results
  printf '[OK] Suite concluida. Logs detalhados: build/fleet_posix/test_logs/\n'
}

run_simulations() {
  load_overlay
  require_command timeout
  mkdir -p "${AUTOMATION_LOG_DIR}"
  local mode logfile collisions missions
  local -a options
  for mode in safe unsafe; do
    options=(--demo)
    [[ "${mode}" != unsafe ]] || options+=(--unsafe)
    logfile="${AUTOMATION_LOG_DIR}/demo-${mode}.log"
    printf '\n[SIMULACAO %s] Sem janela; timeout=%s; log=%s\n' "${mode}" "${DEMO_TIMEOUT}" "${logfile}"
    QT_QPA_PLATFORM=offscreen timeout --signal=TERM --kill-after=10s "${DEMO_TIMEOUT}" \
      ros2 run fleet_posix fleet_supervisor "${options[@]}" 2>&1 | tee "${logfile}"
    missions="$(grep -c 'missao concluida' "${logfile}" || true)"
    collisions="$(grep -oE 'colisoes=[0-9]+' "${logfile}" | tail -n 1)"
    printf '[OK] %s: %s missoes concluidas | %s\n' "${mode}" "${missions}" "${collisions}"
  done
}

run_visual() {
  load_overlay
  [[ -n "${DISPLAY:-}" || -n "${WAYLAND_DISPLAY:-}" ]] || \
    fail 'Sessao grafica ausente. Use make simulate para executar sem janela.'
  local -a options=()
  case "${TASK}" in
    demo) options=(--demo) ;;
    demo-unsafe) options=(--demo --unsafe) ;;
    run-unsafe) options=(--unsafe) ;;
  esac
  printf '\n[WSLg] %s | ROS_DOMAIN_ID=%s.\n' "${TASK}" "${ROS_DOMAIN_ID:-0}"
  if [[ "${TASK}" == demo || "${TASK}" == demo-unsafe ]]; then
    printf 'Demonstracao automatica: encerra apos as tres missoes; Ctrl+C interrompe.\n'
  else
    printf 'Operacao online: enviar, pausar, retomar e status. Use sair ou Ctrl+C para encerrar.\n'
  fi
  exec ros2 run fleet_posix fleet_supervisor "${options[@]}"
}

show_status() {
  printf 'FleetPOSIX - andamento do projeto (referencia documentada)\n\n'
  sed -n '/<!-- andamento:inicio -->/,/<!-- andamento:fim -->/{ /<!--/d; p; }' README.md
  printf '\nEstado LOCAL: estes artefatos sao de execucoes anteriores, nao uma nova validacao.\n'
  if [[ -f install/setup.bash ]]; then
    printf '[ENCONTRADO] Overlay install/setup.bash.\n'
  else
    printf '[PENDENTE] Build local: execute make build.\n'
  fi
  if [[ -f build/fleet_posix/test_results/fleet_posix/fleet_posix_integration.xml && \
        -f build/fleet_posix/test_results/fleet_posix/fleet_posix_shared_state.xml ]]; then
    printf '[ENCONTRADO] Resultados CTest. Use make report para ler, make test para atualizar.\n'
    stat -c '  %y | %n' build/fleet_posix/test_results/fleet_posix/*.xml
  else
    printf '[PENDENTE] Resultados completos de testes locais: execute make test.\n'
  fi
  if [[ -f "${AUTOMATION_LOG_DIR}/demo-safe.log" && -f "${AUTOMATION_LOG_DIR}/demo-unsafe.log" ]]; then
    printf '[ENCONTRADO] Logs da ultima comparacao: log/automation/demo-{safe,unsafe}.log\n'
  else
    printf '[PENDENTE] Comparacao via Make nesta copia: execute make simulate.\n'
  fi
  if command -v git >/dev/null 2>&1; then
    printf '\nUltimo commit local: '
    git log -1 --format='%h %s'
  fi
}

show_ipc() {
  printf '[INSPECAO] Processos da frota nesta maquina (podem pertencer a outras execucoes):\n'
  ps -C fleet_supervisor,amr_agent,turtlesim_node -o pid,ppid,args || true
  printf '\n[INSPECAO] Objetos nomeados; cada nome inclui o PID do supervisor:\n'
  find /dev/shm /dev/mqueue /tmp -maxdepth 1 -name 'fleet_posix_*' -print
  printf 'Nenhum objeto e removido por este alvo.\n'
}

clean_workspace() {
  local name target
  [[ -f src/fleet_posix/package.xml && -f Makefile ]] || fail 'Raiz do workspace nao reconhecida.'
  for name in build install log; do
    target="${WORKSPACE_ROOT}/${name}"
    [[ ! -L "${target}" ]] || fail "Limpeza recusada: ${target} e um link simbolico."
    [[ "$(realpath -m -- "${target}")" == "${WORKSPACE_ROOT}/${name}" ]] || fail 'Destino fora do workspace.'
  done
  printf '[LIMPEZA] Removendo build/, install/ e log/ (incluindo resultados e logs locais).\n'
  rm -rf -- "${WORKSPACE_ROOT}/build" "${WORKSPACE_ROOT}/install" "${WORKSPACE_ROOT}/log"
}

case "${TASK}" in
  help) show_help ;;
  status) show_status ;;
  env) check_environment ;;
  deps)
    load_ros
    command -v rosdep >/dev/null 2>&1 || fail 'rosdep ausente. Instale-o e inicialize sua base antes de make deps.'
    printf '[DEPENDENCIAS] Conferindo rosdep; nenhuma instalacao sera realizada por este alvo.\n'
    rosdep check --from-paths src --ignore-src --rosdistro "${ROS_DISTRO}"
    ;;
  build) build_workspace ;;
  lint|format) check_style ;;
  test) run_tests ;;
  report)
    load_overlay
    require_test_results
    colcon test-result --verbose
    ;;
  simulate) run_simulations ;;
  demo|demo-unsafe|run|run-unsafe) run_visual ;;
  ipc) show_ipc ;;
  slides)
    require_command "${PYTHON}"
    "${PYTHON}" -c 'import reportlab' >/dev/null 2>&1 || \
      fail "reportlab nao esta instalado em ${PYTHON}. Escolha PYTHON com reportlab para gerar os slides."
    "${PYTHON}" docs/generate_slides.py
    ;;
  clean) clean_workspace ;;
  *) fail "Tarefa desconhecida: ${TASK}. Consulte make help." ;;
esac
