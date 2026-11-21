#!/usr/bin/env python3
"""Regressoes reais de processos, comandos e IPC; Turtlesim roda offscreen."""

import argparse
import ctypes
import os
from pathlib import Path
import re
import resource
import signal
import subprocess
import time
import unittest


OPTIONS = None
ENV = dict(os.environ, QT_QPA_PLATFORM='offscreen')
ENV['ROS_DOMAIN_ID'] = os.environ.get('FLEET_TEST_ROS_DOMAIN_ID', str(150 + os.getpid() % 50))
ENV['ROS_AUTOMATIC_DISCOVERY_RANGE'] = 'LOCALHOST'
LIBC = ctypes.CDLL(None)
TARGETS = 'enviar 1 9.0 5.15\nenviar 2 5.5 9.0\nenviar 3 2.0 5.85\n'


# Desativa core dumps nos processos de teste.
def disable_core_dumps():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


class Fleet:
    """Limita cada execucao e preserva o log, inclusive quando um teste falha."""

    # Inicia o supervisor com limites e log proprios.
    def __init__(self, name, *args, env=None, descriptor_limit=None):
        self.path = OPTIONS.log_dir / (name + '.log')
        self.log = self.path.open('w')

        # Aplica os limites no processo filho antes do exec.
        def configure_child():
            disable_core_dumps()
            if descriptor_limit is not None:
                resource.setrlimit(resource.RLIMIT_NOFILE, (descriptor_limit, descriptor_limit))

        self.process = subprocess.Popen(
            [str(OPTIONS.supervisor), *args], stdin=subprocess.PIPE,
            stdout=self.log, stderr=subprocess.STDOUT, text=True,
            env=env or ENV, start_new_session=True, preexec_fn=configure_child,
        )
        self.pid = self.process.pid

    # Entra no contexto do teste.
    def __enter__(self):
        return self

    # Encerra o grupo de processos e remove IPC residual.
    def __exit__(self, exc_type, exc_value, traceback):
        # Nao toca processos ou objetos de outras execucoes do workspace.
        try:
            os.killpg(self.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        self.process.wait(timeout=5)
        self.process.stdin.close()
        self.log.close()
        # Limpeza de contingencia somente depois das assercoes de ausencia de IPC.
        LIBC.shm_unlink(f'/fleet_posix_state_{self.pid}'.encode())
        for robot in range(1, 4):
            LIBC.mq_unlink(f'/fleet_posix_cmd_{self.pid}_{robot}'.encode())
        try:
            self.fifo_path().unlink()
        except FileNotFoundError:
            pass
        if exc_type is not None:
            print(f'\nLog da falha: {self.path}\n{self.text()}', flush=True)

    # Caminho do FIFO de comandos desta execucao.
    def fifo_path(self):
        return Path(f'/tmp/fleet_posix_ctl_{self.pid}')

    def send_fifo(self, commands):
        """Escreve como um terminal externo: abre, escreve linhas completas e fecha."""
        deadline = time.monotonic() + 5
        while not self.fifo_path().exists():
            if time.monotonic() > deadline:
                raise AssertionError(f'FIFO nao criado:\n{self.text()}')
            time.sleep(0.02)
        with self.fifo_path().open('w') as fifo:
            fifo.write(commands)

    # Conteudo atual do log.
    def text(self):
        return self.path.read_text()

    # Aguarda um texto aparecer no log ate o prazo.
    def wait_log(self, token, count=1, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            contents = self.text()
            if contents.count(token) >= count:
                return contents
            if self.process.poll() is not None:
                raise AssertionError(f'Processo terminou antes de {token!r}:\n{contents}')
            time.sleep(0.02)
        raise AssertionError(f'Prazo excedido aguardando {token!r}:\n{self.text()}')

    # Aguarda os tres AMRs ficarem online.
    def online(self):
        self.wait_log('online e pronto', count=3)

    # Envia comandos pelo stdin do supervisor.
    def send(self, commands):
        self.process.stdin.write(commands)
        self.process.stdin.flush()

    # Aguarda o termino e verifica codigo de saida, processos e IPC.
    def finish(self, expected=0, timeout=5):
        result = self.process.wait(timeout=timeout)
        contents = self.text()
        if expected == 0:
            assert result == 0, contents
            assert 'excedeu prazo de shutdown' not in contents, contents
        else:
            assert result != 0, contents
        objects = [Path('/dev/shm') / f'fleet_posix_state_{self.pid}']
        objects += [Path('/dev/mqueue') / f'fleet_posix_cmd_{self.pid}_{i}' for i in range(1, 4)]
        objects.append(self.fifo_path())
        assert not any(path.exists() for path in objects), f'IPC residual: {objects}'
        try:
            os.killpg(self.pid, 0)
        except ProcessLookupError:
            pass
        else:
            raise AssertionError('Grupo ainda possui processos apos waitpid do supervisor')
        return contents


# Cenarios de integracao da frota.
class IntegrationTests(unittest.TestCase):
    # Demo segura: tres missoes e nenhuma colisao.
    def test_demo_safe(self):
        with Fleet('demo_safe', '--demo') as fleet:
            log = fleet.finish(timeout=35)
            self.assertEqual(log.count('missao concluida'), 3)
            self.assertNotIn('[DETECTOR]', log)
            self.assertEqual(log.count('threads finalizadas com pthread_join'), 3)

    # Demo insegura: tres missoes e colisoes detectadas.
    def test_demo_unsafe(self):
        with Fleet('demo_unsafe', '--unsafe', '--demo') as fleet:
            log = fleet.finish(timeout=35)
            self.assertEqual(log.count('missao concluida'), 3)
            self.assertIn('[DETECTOR]', log)
            self.assertRegex(log, r'colisoes=[1-9]\d*')

    # Detector de colisoes ativo sem novas entradas.
    def test_online_collision_detection_without_input(self):
        with Fleet('online_collision', '--unsafe') as fleet:
            fleet.online()
            fleet.send(TARGETS)
            fleet.wait_log('missao concluida', count=3)
            self.assertIn('[DETECTOR]', fleet.text())
            fleet.send('status\nsair\n')
            self.assertRegex(fleet.finish(), r'colisoes=[1-9]\d*')

    # Shutdown com dono do corredor pausado e AMRs aguardando.
    def test_shutdown_with_paused_owner_and_corridor_waiters(self):
        with Fleet('paused_owner') as fleet:
            fleet.online()
            fleet.send(TARGETS)
            log = fleet.wait_log('adquiriu o semaforo do corredor')
            owner = re.search(r'\[AMR (\d) \| pid=\d+\] adquiriu', log).group(1)
            fleet.send(f'pausar {owner}\n')
            fleet.wait_log('pausado pelo operador')
            fleet.wait_log('aguardando sem_wait', count=3)
            fleet.send('sair\n')
            log = fleet.finish()
            self.assertIn('liberou o corredor', log)
            self.assertEqual(log.count('threads finalizadas com pthread_join'), 3)

    # Comandos pelo FIFO e recusa de destino na regiao critica.
    def test_fifo_control_channel_and_corridor_destination(self):
        with Fleet('fifo_control') as fleet:
            fleet.online()
            self.assertIn(str(fleet.fifo_path()), fleet.text())
            # Destino dentro da regiao critica e recusado (nao chega a fila POSIX).
            fleet.send_fifo('enviar 1 5.5 5.5\n')
            fleet.wait_log('Destino dentro da regiao critica')
            # Varios escritores sucessivos: cada echo fecha o FIFO sem gerar EOF.
            for command in TARGETS.splitlines():
                fleet.send_fifo(command + '\n')
            fleet.wait_log('missao concluida', count=3, timeout=30)
            self.assertNotIn('[DETECTOR]', fleet.text())
            fleet.send_fifo('status\nsair\n')
            log = fleet.finish()
            self.assertEqual(log.count('[FIFO] enviar'), 4)
            self.assertEqual(log.count('threads finalizadas com pthread_join'), 3)

    # Buffer cheio nao bloqueia retomada nem shutdown.
    def test_full_buffer_keeps_resume_and_shutdown_responsive(self):
        with Fleet('full_buffer') as fleet:
            fleet.online()
            fleet.send('enviar 1 9.0 5.15\n')
            fleet.wait_log('missao recebida')
            fleet.send('pausar 1\n')
            fleet.wait_log('pausado pelo operador')
            fleet.send('enviar 1 9.0 5.15\n' * 40)
            fleet.wait_log('buffer de missoes cheio')
            fleet.send('retomar 1\n')
            fleet.wait_log('retomado pelo operador')
            fleet.send('pausar 1\n')
            fleet.wait_log('pausado pelo operador', count=2)
            fleet.send('status\nsair\n')
            self.assertEqual(fleet.finish().count('threads finalizadas com pthread_join'), 3)

    # SIGINT com linha incompleta no terminal.
    def test_sigint_idle_console_without_newline(self):
        with Fleet('sigint_idle') as fleet:
            fleet.online()
            fleet.send('stat')
            os.killpg(fleet.pid, signal.SIGINT)
            self.assertEqual(fleet.finish().count('threads finalizadas com pthread_join'), 3)

    # SIGTERM com missao pausada.
    def test_sigterm_paused(self):
        with Fleet('sigterm_paused') as fleet:
            fleet.online()
            fleet.send('enviar 1 9.0 5.15\n')
            fleet.wait_log('missao recebida')
            fleet.send('pausar 1\n')
            fleet.wait_log('pausado pelo operador')
            os.kill(fleet.pid, signal.SIGTERM)
            self.assertEqual(fleet.finish().count('threads finalizadas com pthread_join'), 3)

    # Limpeza apos falha parcial de IPC por limite de descritores.
    def test_partial_initialization_cleanup(self):
        # Com limite 3 o loader falha antes do main; 4..6 exercitam IPC parcial.
        for limit in (4, 5, 6):
            with self.subTest(descriptors=limit):
                with Fleet(f'partial_init_{limit}', '--demo', descriptor_limit=limit) as fleet:
                    log = fleet.finish(expected=1)
                    self.assertIn('Too many open files', log)

    # Falha ao iniciar o Turtlesim encerra a frota.
    def test_simulator_startup_failure(self):
        env = dict(ENV, QT_QPA_PLATFORM='fleet_posix_invalid_test_platform')
        with Fleet('simulator_failure', '--demo', env=env) as fleet:
            log = fleet.finish(expected=1, timeout=10)
            self.assertIn('Um processo filho terminou', log)

    # Morte de um agente encerra a frota.
    def test_agent_crash_requests_shutdown(self):
        with Fleet('agent_crash') as fleet:
            fleet.online()
            pid = int(re.search(r'\[AMR 1 \| pid=(\d+)\]', fleet.text()).group(1))
            os.kill(pid, signal.SIGKILL)
            log = fleet.finish(expected=1)
            self.assertIn('Um processo filho terminou', log)

    # Argumentos invalidos e execucao do demo.sh.
    def test_invalid_arguments_and_script_locations(self):
        result = subprocess.run(
            [str(OPTIONS.agent), '--id', 'abc', '--shm', '/unused',
             '--queue', '/unused', '--log-fd', '1'],
            env=ENV, capture_output=True, text=True, timeout=5,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Argumentos internos', result.stderr)
        with Fleet('invalid_option', '--unknown') as fleet:
            self.assertIn('Opcao desconhecida', fleet.finish(expected=1))
        script = Path(__file__).resolve().parents[1] / 'scripts/demo.sh'
        workspace = Path(__file__).resolve().parents[3]
        installed_script = workspace / 'install/fleet_posix/lib/fleet_posix/demo.sh'
        for path in (script, installed_script):
            if path == installed_script and not path.exists():
                continue
            with self.subTest(script=path):
                result = subprocess.run(
                    ['bash', str(path), '--help'], cwd='/tmp', env=ENV,
                    capture_output=True, text=True, timeout=10,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn('Uso: fleet_supervisor', result.stdout)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--supervisor', required=True, type=Path)
    parser.add_argument('--agent', required=True, type=Path)
    parser.add_argument('--log-dir', required=True, type=Path)
    OPTIONS, remaining = parser.parse_known_args()
    OPTIONS.log_dir.mkdir(parents=True, exist_ok=True)
    unittest.main(argv=[__file__, *remaining], verbosity=2)
