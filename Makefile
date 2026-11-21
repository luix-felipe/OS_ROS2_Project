# FleetPOSIX: execute no Ubuntu/WSL, na raiz do repositorio.
# Make orquestra as ferramentas; CMake/colcon continuam responsaveis pelo build.
# Todos os alvos sao tarefas: nao use datas de arquivos para pular verificacoes.
.DEFAULT_GOAL := help
SHELL := /bin/bash
.SHELLFLAGS := -eu -o pipefail -c

# Evita executar build, testes e simulacoes simultaneamente com make -j.
.NOTPARALLEL:
.PHONY: help status env deps build lint format test report verify simulate \
        demo demo-unsafe run run-unsafe ipc slides clean

ROS_DISTRO ?= jazzy
ROS_SETUP ?= /opt/ros/$(ROS_DISTRO)/setup.bash
ROS_DOMAIN_ID ?= 0
PYTHON ?= python3
JOBS ?= 2
DEMO_TIMEOUT ?= 45s
export ROS_DISTRO ROS_SETUP ROS_DOMAIN_ID PYTHON JOBS DEMO_TIMEOUT

# O script concentra diagnosticos, tratamento de erros e caminhos com espacos.
# Cada tarefa carrega o ROS/overlay necessario; nao e preciso executar source.
help status env deps lint format report ipc slides clean:
	@bash scripts/workspace.sh $@

# Compila o workspace.
build:
	@bash scripts/workspace.sh build

# Dependencia unica: make verify constroi uma vez e so testa se o build passar.
test: build
	@bash scripts/workspace.sh test

# A ordem e intencional: diagnostico -> build -> estilo -> regressao offscreen.
# A suite de integracao ja executa as demos segura e insegura.
verify: env build lint test
	@printf '\n[OK] Verificacao concluida: ambiente, build, estilo e testes.\n'
	@printf 'Dependencias rosdep: verificacao separada com make deps.\n'

# Simulacao automatica sem janela, com logs separados e resumo dos dois modos.
simulate: build
	@bash scripts/workspace.sh simulate

# Interface grafica e operacao interativa sao opt-in; nunca entram em verify.
demo demo-unsafe run run-unsafe: build
	@bash scripts/workspace.sh $@
