#!/usr/bin/env python3
"""Gera a apresentacao animada do FleetPOSIX em PowerPoint."""

from pathlib import Path

from pptx import Presentation
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_CONNECTOR, MSO_SHAPE
from pptx.enum.text import MSO_ANCHOR, PP_ALIGN
from pptx.util import Inches, Pt


ROOT = Path(__file__).resolve().parent
MEDIA = ROOT / "media"
DESTINATION = ROOT / "FleetPOSIX_apresentacao_animada.pptx"


def rgb(value):
    value = value.lstrip("#")
    return RGBColor(*(int(value[index:index + 2], 16) for index in (0, 2, 4)))


NAVY = rgb("#101F35")
INK = rgb("#182D46")
MUTED = rgb("#55677A")
PAPER = rgb("#F4F7FB")
WHITE = rgb("#FFFFFF")
TEAL = rgb("#087F83")
BLUE = rgb("#2563D8")
AMBER = rgb("#A95608")
LIGHT = rgb("#E1EAF4")
RED = rgb("#B42318")


PRESENTATION = Presentation()
PRESENTATION.slide_width = Inches(13.333)
PRESENTATION.slide_height = Inches(7.5)
PRESENTATION.core_properties.title = "FleetPOSIX - Concorrencia e IPC POSIX"
PRESENTATION.core_properties.author = (
    "Luiz Felipe Souza da Silva, Elian Lima Pereira e Diogo de Mello Silva")
BLANK = PRESENTATION.slide_layouts[6]


def add_text(slide, x, y, width, height, value, size=18, color=INK,
             bold=False, font="Aptos", align=PP_ALIGN.LEFT,
             valign=MSO_ANCHOR.TOP):
    box = slide.shapes.add_textbox(Inches(x), Inches(y), Inches(width), Inches(height))
    frame = box.text_frame
    frame.clear()
    frame.margin_left = 0
    frame.margin_right = 0
    frame.margin_top = 0
    frame.margin_bottom = 0
    frame.vertical_anchor = valign
    paragraph = frame.paragraphs[0]
    paragraph.alignment = align
    run = paragraph.add_run()
    run.text = value
    run.font.name = font
    run.font.size = Pt(size)
    run.font.bold = bold
    run.font.color.rgb = color
    return box


def add_lines(slide, x, y, width, values, size=17, color=INK, gap=0.36,
              bullet=False, bold_first=False):
    for index, value in enumerate(values):
        prefix = "•  " if bullet else ""
        add_text(slide, x, y + index * gap, width, gap, prefix + value,
                 size=size, color=color, bold=bold_first and index == 0)


def add_rect(slide, x, y, width, height, fill, radius=True,
             line=None, line_width=1):
    shape_type = MSO_SHAPE.ROUNDED_RECTANGLE if radius else MSO_SHAPE.RECTANGLE
    shape = slide.shapes.add_shape(
        shape_type, Inches(x), Inches(y), Inches(width), Inches(height))
    shape.fill.solid()
    shape.fill.fore_color.rgb = fill
    shape.line.color.rgb = fill if line is None else line
    shape.line.width = Pt(line_width)
    return shape


def add_line(slide, x1, y1, x2, y2, color=BLUE, width=2):
    line = slide.shapes.add_connector(
        MSO_CONNECTOR.STRAIGHT, Inches(x1), Inches(y1), Inches(x2), Inches(y2))
    line.line.color.rgb = color
    line.line.width = Pt(width)
    return line


def add_panel(slide, x, y, width, height, title, details, accent=TEAL):
    add_rect(slide, x, y, width, height, WHITE)
    add_rect(slide, x, y, 0.08, height, accent, radius=False)
    add_text(slide, x + 0.23, y + 0.2, width - 0.4, 0.35, title,
             17, INK, True)
    add_lines(slide, x + 0.23, y + 0.65, width - 0.4, details,
              size=12, color=MUTED, gap=0.28)


def begin_slide(number, title, subtitle):
    slide = PRESENTATION.slides.add_slide(BLANK)
    background = slide.background.fill
    background.solid()
    background.fore_color.rgb = PAPER
    add_rect(slide, 0.55, 0.47, 0.08, 0.42, TEAL, radius=False)
    add_text(slide, 0.78, 0.43, 11.7, 0.48, title, 27, INK, True)
    add_text(slide, 0.78, 0.95, 11.7, 0.3, subtitle, 12, MUTED)
    add_text(slide, 0.55, 7.18, 10.5, 0.18,
             "FleetPOSIX  |  Sistemas Operacionais Embarcados  |  23/11/2026",
             8, MUTED)
    add_text(slide, 12.15, 7.18, 0.6, 0.18, f"{number} / 6", 8, MUTED,
             align=PP_ALIGN.RIGHT)
    return slide


def add_route_map(slide, x, y, width=4.5, height=4.5):
    add_rect(slide, x, y, width, height, WHITE)
    cx, cy = x + width / 2, y + height / 2
    circle = slide.shapes.add_shape(
        MSO_SHAPE.OVAL, Inches(cx - 0.92), Inches(cy - 0.92),
        Inches(1.84), Inches(1.84))
    circle.fill.solid()
    circle.fill.fore_color.rgb = rgb("#E6F2F3")
    circle.line.color.rgb = TEAL
    circle.line.dash_style = 2
    add_line(slide, x + 0.45, cy - 0.15, x + width - 0.45, cy - 0.15, TEAL, 3)
    add_line(slide, cx, y + 0.45, cx, y + height - 0.45, AMBER, 3)
    add_line(slide, x + width - 0.45, cy + 0.15, x + 0.45, cy + 0.15, BLUE, 3)
    add_text(slide, x + 0.25, cy - 0.55, 1.0, 0.3, "AMR 1", 11, TEAL, True)
    add_text(slide, cx + 0.15, y + 0.25, 1.0, 0.3, "AMR 2", 11, AMBER, True)
    add_text(slide, x + width - 1.15, cy + 0.35, 1.0, 0.3, "AMR 3", 11, BLUE, True)


# Slide 1
slide = begin_slide(
    1, "FleetPOSIX",
    "Concorrência, sincronização e IPC POSIX com três AMRs independentes")
add_text(slide, 0.75, 1.65, 6.4, 0.55, "Três processos. Uma região compartilhada.",
         25, INK, True)
add_lines(slide, 0.75, 2.45, 6.2, [
    "Operador envia missões durante a execução.",
    "Semáforo coordena o corredor central.",
    "Modo inseguro expõe colisões lógicas.",
], size=17, gap=0.52, bullet=True)
add_rect(slide, 0.75, 4.35, 6.25, 0.7, NAVY)
add_text(slide, 1.02, 4.57, 5.8, 0.3,
         "Controle: POSIX   |   Visualização: ROS 2 / Turtlesim",
         15, WHITE, True)
add_text(slide, 0.75, 5.45, 6.1, 0.4,
         "Ubuntu 24.04 / WSL 2  •  ROS 2 Jazzy  •  C++17", 13, MUTED)
add_rect(slide, 8.0, 1.35, 4.65, 4.95, WHITE)
slide.shapes.add_picture(str(MEDIA / "fleet-safe.gif"), Inches(8.34), Inches(1.72),
                         width=Inches(3.95), height=Inches(3.95))
add_text(slide, 8.35, 5.82, 3.95, 0.3, "GIF real — modo seguro",
         12, TEAL, True, align=PP_ALIGN.CENTER)
add_text(slide, 0.75, 6.35, 11.5, 0.3,
         "Equipe: Luiz Felipe Souza da Silva  •  Elian Lima Pereira  •  Diogo de Mello Silva",
         12, INK, True)


# Slide 2
slide = begin_slide(
    2, "Arquitetura de processos e threads",
    "O supervisor cria todos os filhos antes da thread agregadora de logs")
add_panel(slide, 0.7, 1.45, 3.2, 1.25, "fleet_supervisor", [
    "terminal + FIFO", "detector, shutdown e waitpid"], TEAL)
add_panel(slide, 5.05, 1.45, 2.65, 1.05, "turtlesim_node", [
    "pose, cmd_vel e /spawn"], BLUE)
add_line(slide, 3.9, 2.02, 5.05, 2.02, BLUE, 2)
add_text(slide, 4.25, 1.76, 0.6, 0.25, "exec", 10, BLUE, True)
for index, x in enumerate((0.7, 4.32, 7.94), 1):
    add_panel(slide, x, 4.0, 3.15, 1.45, f"amr_agent {index}", [
        "pthread: rclcpp::spin",
        "pthread: mq_receive / produtor",
        "pthread: controle / consumidor",
    ], (TEAL, AMBER, BLUE)[index - 1])
    add_line(slide, 2.3, 2.7, x + 1.55, 4.0, TEAL, 1.5)
add_panel(slide, 8.3, 1.35, 4.35, 1.75, "IPC explícito", [
    "fila POSIX por AMR + pipe de eventos",
    "memória e semáforo compartilhados",
    "FIFO de comandos externos",
], AMBER)
add_text(slide, 0.75, 6.15, 11.8, 0.35,
         "Supervisor: 1 pthread de logs  |  Cada AMR: 3 pthreads explícitas + runtime ROS",
         14, MUTED, True, align=PP_ALIGN.CENTER)


# Slide 3
slide = begin_slide(
    3, "Onde cada conceito aparece",
    "O plano de controle mantém as APIs POSIX visíveis no código")
rows = [
    ("Processos", "fork, execl, _exit, exit e waitpid"),
    ("Threads POSIX", "pthread_create e pthread_join"),
    ("Mutexes", "PROCESS_SHARED + ROBUST; pose, movimento e buffer"),
    ("Condição", "pthread_cond_timedwait com CLOCK_MONOTONIC"),
    ("Semáforos", "buffer produtor–consumidor + corredor pshared=1"),
    ("Pipe / FIFO", "LogEvent atômico; mkfifo + poll para comandos"),
    ("Filas POSIX", "mq_open, mq_send, mq_receive e mq_unlink"),
    ("Memória / sinais", "shm_open + mmap; SIGINT, SIGTERM e SIGKILL"),
]
for index, (concept, evidence) in enumerate(rows):
    y = 1.48 + index * 0.62
    add_rect(slide, 0.75, y, 11.8, 0.48, WHITE if index % 2 == 0 else LIGHT,
             radius=False)
    add_text(slide, 0.95, y + 0.1, 2.4, 0.25, concept, 14, INK, True)
    add_text(slide, 3.2, y + 0.1, 8.9, 0.25, evidence, 13, INK)
add_text(slide, 0.75, 6.6, 11.7, 0.3,
         "O ROS 2 movimenta e desenha; concorrência, sincronização e IPC são implementados pela equipe.",
         13, MUTED, True, align=PP_ALIGN.CENTER)


# Slide 4
slide = begin_slide(
    4, "Exclusão mútua no corredor",
    "Mesmas rotas; o semáforo decide se a convergência será segura")
add_route_map(slide, 0.75, 1.45, 4.75, 4.75)
add_lines(slide, 6.0, 1.7, 6.25, [
    "1. Aproximação: parar antes da região crítica.",
    "2. Ocupado: aguardar com sem_clockwait.",
    "3. Adquirido: atravessar o corredor central.",
    "4. Saída: liberar com sem_post.",
], size=17, gap=0.66)
add_rect(slide, 6.0, 4.7, 6.15, 0.72, NAVY)
add_text(slide, 6.35, 4.93, 5.5, 0.3,
         "sem_init(&state->corridor, 1, 1)", 17, WHITE, True, "Consolas")
add_text(slide, 6.0, 5.72, 6.2, 0.55,
         "O modo --unsafe desativa essa aquisição e permite que as três rotas ocupem o centro simultaneamente.",
         13, MUTED)


# Slide 5
slide = begin_slide(
    5, "Resultado animado: seguro × inseguro",
    "GIFs capturados de execuções reais do Turtlesim em 23/11/2026")
add_rect(slide, 0.55, 1.35, 6.0, 5.25, WHITE)
add_rect(slide, 6.78, 1.35, 6.0, 5.25, WHITE)
add_text(slide, 0.85, 1.55, 5.4, 0.35, "SEGURO", 19, TEAL, True,
         align=PP_ALIGN.CENTER)
add_text(slide, 7.08, 1.55, 5.4, 0.35, "INSEGURO", 19, AMBER, True,
         align=PP_ALIGN.CENTER)
slide.shapes.add_picture(str(MEDIA / "fleet-safe.gif"), Inches(1.43), Inches(1.98),
                         width=Inches(4.25), height=Inches(4.25))
slide.shapes.add_picture(str(MEDIA / "fleet-unsafe.gif"), Inches(7.66), Inches(1.98),
                         width=Inches(4.25), height=Inches(4.25))
add_rect(slide, 0.85, 6.05, 5.4, 0.42, TEAL)
add_rect(slide, 7.08, 6.05, 5.4, 0.42, AMBER)
add_text(slide, 1.0, 6.15, 5.1, 0.2,
         "3 missões  •  travessias em série  •  0 colisões",
         11, WHITE, True, align=PP_ALIGN.CENTER)
add_text(slide, 7.23, 6.15, 5.1, 0.2,
         "3 missões  •  convergência  •  2 colisões",
         11, WHITE, True, align=PP_ALIGN.CENTER)


# Slide 6
slide = begin_slide(
    6, "Demonstração e conclusão",
    "Execução reproduzível, observável e com encerramento determinístico")
add_panel(slide, 0.7, 1.45, 6.0, 2.05, "Demonstração", [
    "make demo          # seguro, janela WSLg",
    "make demo-unsafe   # condição de corrida",
    "make run           # comandos online + FIFO",
    "make verify        # build, estilo e testes",
], TEAL)
add_panel(slide, 6.95, 1.45, 5.7, 2.05, "Evidências", [
    "12 cenários Python + mutex robusto",
    "0 colisões seguro / 2 inseguro",
    "SIGTERM → SIGKILL → waitpid",
    "sem processos ou IPCs residuais",
], AMBER)
add_rect(slide, 0.7, 3.9, 11.95, 1.1, NAVY)
add_text(slide, 1.0, 4.15, 11.35, 0.55,
         "Resultado: o Turtlesim torna a corrida visível; as APIs POSIX tornam a solução demonstrável.",
         19, WHITE, True, align=PP_ALIGN.CENTER, valign=MSO_ANCHOR.MIDDLE)
add_text(slide, 0.75, 5.45, 11.8, 0.35,
         "Evolução: substituir a abstração 2D por Gazebo/AMRs reais sem alterar o plano de controle POSIX.",
         14, MUTED, align=PP_ALIGN.CENTER)
repository = add_text(slide, 0.75, 6.15, 11.8, 0.3,
                      "github.com/luix-felipe/OS_ROS2_Project",
                      13, BLUE, True, align=PP_ALIGN.CENTER)
repository.text_frame.paragraphs[0].runs[0].hyperlink.address = (
    "https://github.com/luix-felipe/OS_ROS2_Project")


PRESENTATION.save(DESTINATION)
print(f"Apresentacao gerada: {DESTINATION}")
