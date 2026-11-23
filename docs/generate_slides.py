#!/usr/bin/env python3
"""Gera os seis slides em PDF. Dependencia opcional: reportlab."""

from pathlib import Path
import math

from reportlab.lib.colors import HexColor
from reportlab.pdfgen import canvas


WIDTH, HEIGHT = 960, 540
NAVY = '#101F35'
INK = '#182D46'
MUTED = '#55677A'
PAPER = '#F4F7FB'
WHITE = '#FFFFFF'
TEAL = '#087F83'
BLUE = '#2563D8'
AMBER = '#A95608'
LIGHT = '#E1EAF4'
DESTINATION = Path(__file__).with_name('FleetPOSIX_apresentacao.pdf')
EVIDENCE = Path(__file__).parent / 'evidence' / '2026-11-23'
PDF = canvas.Canvas(str(DESTINATION), pagesize=(WIDTH, HEIGHT))
PDF.setTitle('FleetPOSIX - Concorrencia e IPC POSIX')
PDF.setAuthor('Luiz Felipe Souza da Silva, Elian Lima Pereira e Diogo de Mello Silva')


# Desenha um retangulo arredondado preenchido.
def rect(x, y, width, height, fill, radius=10):
    PDF.setFillColor(HexColor(fill))
    PDF.roundRect(x, y, width, height, radius, stroke=0, fill=1)


# Escreve um texto.
def text(x, y, value, size=16, color=INK, font='Helvetica'):
    PDF.setFillColor(HexColor(color))
    PDF.setFont(font, size)
    PDF.drawString(x, y, value)


# Escreve varias linhas com espacamento fixo.
def lines(x, y, values, size=17, color=INK, gap=28, font='Helvetica'):
    for index, value in enumerate(values):
        text(x, y - index * gap, value, size, color, font)


# Desenha uma seta.
def arrow(x1, y1, x2, y2, color=BLUE, width=2):
    PDF.setStrokeColor(HexColor(color))
    PDF.setFillColor(HexColor(color))
    PDF.setLineWidth(width)
    PDF.line(x1, y1, x2, y2)
    angle = math.atan2(y2 - y1, x2 - x1)
    path = PDF.beginPath()
    path.moveTo(x2, y2)
    for direction in (angle + 2.6, angle - 2.6):
        path.lineTo(x2 + 9 * math.cos(direction), y2 + 9 * math.sin(direction))
    path.close()
    PDF.drawPath(path, stroke=0, fill=1)


# Inicia um slide com titulo, subtitulo e rodape.
def begin(number, title, subtitle):
    PDF.setFillColor(HexColor(PAPER))
    PDF.rect(0, 0, WIDTH, HEIGHT, stroke=0, fill=1)
    rect(40, 477, 6, 28, TEAL, radius=2)
    text(60, 482, title, 29, INK, 'Helvetica-Bold')
    text(60, 449, subtitle, 14, MUTED)
    text(40, 22, 'FleetPOSIX  |  Sistemas Operacionais Embarcados  |  23/11/2026', 10, MUTED)
    text(890, 22, f'{number} / 6', 10, MUTED)


# Desenha um cartao com titulo e detalhes.
def box(x, y, width, height, title, detail, color=WHITE):
    rect(x, y, width, height, color)
    text(x + 18, y + height - 30, title, 18, INK, 'Helvetica-Bold')
    lines(x + 18, y + height - 57, detail, size=13, gap=21, color=MUTED)


# Desenha o mapa das rotas e do corredor.
def map_routes(x, y, scale=26):
    rect(x, y, scale * 11, scale * 11, WHITE)
    cx, cy = x + 5.5 * scale, y + 5.5 * scale
    PDF.setFillColor(HexColor('#E6F2F3'))
    PDF.circle(cx, cy, 2.35 * scale, stroke=0, fill=1)
    PDF.setStrokeColor(HexColor(TEAL))
    PDF.setDash(4, 3)
    PDF.circle(cx, cy, 1.35 * scale, stroke=1, fill=0)
    PDF.setDash()
    routes = [((2, 5.15), (9, 5.15), TEAL, 'AMR 1'),
              ((5.5, 2), (5.5, 9), AMBER, 'AMR 2'),
              ((9, 5.85), (2, 5.85), BLUE, 'AMR 3')]
    for start, end, color, label in routes:
        sx, sy = x + start[0] * scale, y + start[1] * scale
        ex, ey = x + end[0] * scale, y + end[1] * scale
        arrow(sx, sy, ex, ey, color, width=2.5)
        PDF.setFillColor(HexColor(color))
        PDF.circle(sx, sy, 5, stroke=0, fill=1)
        text(sx - 20, sy - 22 if label != 'AMR 3' else sy + 14, label, 11, color,
             'Helvetica-Bold')
    text(x + 15, y + 16, 'Abstração 2D - sem obstáculos físicos', 11, MUTED)


begin(1, 'FleetPOSIX', 'Concorrência, sincronização e IPC POSIX com três AMRs')
text(60, 380, 'Três processos independentes.', 27, INK, 'Helvetica-Bold')
text(60, 342, 'Uma região compartilhada.', 27, INK, 'Helvetica-Bold')
lines(60, 286, ['Operador envia missões durante a execução.',
                'Semáforo coordena a travessia central.',
                'Modo inseguro expõe colisões lógicas.'], size=18, gap=34)
rect(60, 101, 480, 58, NAVY)
text(78, 123, 'Controle: POSIX   |   Pose e visualização: ROS 2', 17, WHITE, 'Helvetica-Bold')
text(60, 72, 'Ubuntu 24.04 / WSL 2   •   ROS 2 Jazzy   •   C++17', 13, MUTED)
text(60, 48, 'Equipe: Luiz Felipe Souza da Silva  •  Elian Lima Pereira  •  Diogo de Mello Silva', 12, INK, 'Helvetica-Bold')
map_routes(605, 109, 26)
PDF.showPage()

begin(2, 'Arquitetura de processos e threads', 'Todos os fork/exec ocorrem antes da thread agregadora de logs')
box(60, 296, 255, 112, 'fleet_supervisor', ['Terminal e estado da frota', 'Monitoramento, shutdown e waitpid'])
box(373, 324, 205, 84, 'turtlesim_node', ['Pose, cmd_vel e serviço /spawn'])
arrow(315, 365, 365, 365)
text(321, 383, 'exec', 11, BLUE)
for index, xpos in enumerate((60, 350, 640), 1):
    box(xpos, 130, 260, 116, f'amr_agent {index}',
        ['pthread: rclcpp::spin', 'pthread: mq_receive e produção', 'pthread: controle e consumo'])
    arrow(188, 295, xpos + 130, 252, TEAL)
rect(640, 296, 260, 112, LIGHT)
text(658, 377, 'IPC explícito', 18, INK, 'Helvetica-Bold')
lines(658, 353, ['Filas de comandos por AMR', 'Memória e semáforo compartilhados',
                 'Pipe de eventos para o supervisor', 'FIFO de comandos externos'],
      size=12, gap=17)
text(60, 77, 'Supervisor: 1 pthread de logs  |  Cada agente: 3 pthreads da aplicação + runtime ROS', 14, MUTED)
PDF.showPage()

begin(3, 'Onde cada conceito aparece', 'O plano de controle mantém as APIs POSIX visíveis no código')
rows = [
    ('Processos', 'fork, execl, _exit, exit e waitpid'),
    ('Threads', 'pthread_create e pthread_join'),
    ('Mutexes', 'Estado compartilhado, pose, movimento e buffer'),
    ('Condição', 'pthread_cond_timedwait aguarda nova pose'),
    ('Semáforos', 'Buffer produtor-consumidor + corredor (pshared=1)'),
    ('Pipe', 'LogEvent menor que PIPE_BUF: escrita atômica'),
    ('Named pipe (FIFO)', 'mkfifo + poll: comandos de outro terminal'),
    ('Filas POSIX', 'mq_open, mq_send, mq_receive, mq_unlink'),
    ('Memória e sinais', 'shm_open/mmap; SIGINT e SIGTERM no shutdown'),
]
for index, (concept, evidence) in enumerate(rows):
    ypos = 392 - index * 35
    rect(60, ypos - 10, 840, 31, WHITE if index % 2 == 0 else LIGHT, radius=4)
    text(76, ypos, concept, 15, INK, 'Helvetica-Bold')
    text(271, ypos, evidence, 14, INK)
text(60, 54, 'O executor ROS cuida da simulação; sincronização e IPC permanecem explícitos.', 14, MUTED)
PDF.showPage()

begin(4, 'Exclusão mútua no corredor', 'Rotas separadas nas faixas horizontais; interseções dentro da região protegida')
map_routes(60, 86, 29)
lines(435, 379, ['1. Aproximação: parar e solicitar o semáforo.',
                 '2. Ocupado: aguardar com prazo por tentativa.',
                 '3. Adquirido: atravessar a região central.',
                 '4. Saída: liberar com sem_post.'], size=17, gap=49)
rect(435, 112, 465, 54, NAVY)
text(454, 132, 'sem_init(&state->corridor, 1, 1)', 16, WHITE, 'Courier')
lines(435, 89, ['Centro (5.5, 5.5); raios de aquisição 2.35 e núcleo 1.35.',
                'A pausa mantém a reserva; o shutdown a libera.'], size=12, gap=19, color=MUTED)
PDF.showPage()

begin(5, 'Resultado real: seguro × inseguro',
      'Capturas do Turtlesim em 23/11/2026; mesmas rotas, com e sem exclusão mútua')
rect(50, 92, 410, 330, WHITE)
rect(500, 92, 410, 330, WHITE)
text(67, 394, 'SEGURO', 19, TEAL, 'Helvetica-Bold')
text(517, 394, 'INSEGURO', 19, AMBER, 'Helvetica-Bold')
PDF.drawImage(str(EVIDENCE / 'safe-corredor-2.png'), 117, 120, 276, 276,
              preserveAspectRatio=True, mask='auto')
PDF.drawImage(str(EVIDENCE / 'unsafe-convergencia.png'), 567, 120, 276, 276,
              preserveAspectRatio=True, mask='auto')
rect(67, 103, 376, 37, TEAL, radius=5)
rect(517, 103, 376, 37, AMBER, radius=5)
text(84, 115, '3 missões  •  travessias em série  •  0 colisões', 13, WHITE,
     'Helvetica-Bold')
text(534, 115, '3 missões  •  convergência simultânea  •  2 colisões', 13, WHITE,
     'Helvetica-Bold')
text(50, 57, 'make verify: build com -Werror, estilo e 13 cenários aprovados; sem processos ou IPCs residuais.',
     13, MUTED)
PDF.showPage()

begin(6, 'Automação e demonstração com Make',
      'No Ubuntu/WSL, cada alvo carrega o ambiente ROS e explica a etapa executada')
rect(60, 171, 840, 252, NAVY)
workflow = [
    ('make help', 'Consultar alvos, opções e sequência sugerida.'),
    ('make status', 'Ler o andamento, as evidências e as pendências.'),
    ('make env', 'Conferir ferramentas, pacotes ROS e acesso aos IPCs.'),
    ('make verify', 'Compilar, conferir estilo e rodar os 13 cenários sem janela.'),
    ('make simulate', 'Comparar seguro/inseguro offscreen e salvar logs.'),
    ('make demo', 'Exibir a demonstração segura no WSLg.'),
    ('make run', 'Enviar missões online e controlar pausa/retomada.'),
]
for index, (command, description) in enumerate(workflow):
    ypos = 395 - index * 31
    text(80, ypos, command, 16, WHITE, 'Courier')
    text(320, ypos, description, 14, WHITE)
box(60, 64, 405, 89, 'Logs e inspeção',
    ['log/automation/  |  make report  |  make ipc',
     'Uma falha interrompe a sequência de validação.'])
box(495, 64, 405, 89, 'Operação online',
    ['enviar, pausar, retomar, status e sair',
     'Também por FIFO: echo "status" > /tmp/fleet_posix_ctl_<pid>'])
text(60, 44, 'Controle e IPC permanecem POSIX; Turtlesim é uma abstração visual 2D.', 12, MUTED)
PDF.showPage()

PDF.save()
print(f'Apresentação gerada: {DESTINATION}')
