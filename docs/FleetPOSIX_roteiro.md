# Roteiro — apresentação FleetPOSIX

Duração estimada: **6 a 7 minutos**.

## Slide 1 — Motivação (50 s)

“Nosso problema é coordenar três robôs independentes que compartilham um
corredor de fábrica. O operador envia missões durante a execução. O Turtlesim
serve como representação visual dos AMRs, mas todo o plano de controle foi
implementado diretamente com mecanismos POSIX.”

## Slide 2 — Arquitetura (65 s)

“O supervisor cria o Turtlesim e três processos agentes com `fork` e `exec`.
Cada agente possui três pthreads: uma para o executor ROS, uma produtora que
recebe comandos e uma consumidora que controla o movimento. O supervisor
recolhe todos os processos usando `waitpid`.”

## Slide 3 — Conceitos da disciplina (70 s)

Apontar rapidamente uma linha de cada vez. Destacar:

- mutex robusto e compartilhado entre processos;
- semáforo do corredor e semáforos do buffer;
- pipe para logs e FIFO para comandos externos;
- fila POSIX por AMR;
- memória compartilhada para o estado global.

## Slide 4 — Seção crítica (55 s)

“As três rotas se cruzam somente dentro da região crítica. No modo seguro, o
AMR para antes da região, executa `sem_clockwait`, atravessa e libera com
`sem_post`. No modo inseguro, essa aquisição é desativada.”

## Slide 5 — Resultado animado (90 s)

Deixar os GIFs executarem por alguns segundos.

“À esquerda, apenas um AMR atravessa enquanto os outros aguardam: três missões
e zero colisões. À direita, sem o semáforo, os três convergem. O supervisor
detectou duas colisões lógicas pela distância entre as poses.”

Se houver tempo, executar ao vivo:

```bash
make demo
make demo-unsafe
```

## Slide 6 — Validação e conclusão (60 s)

“Além da demonstração, temos doze cenários Python e um teste C++ do mutex
robusto. O shutdown possui contingência com SIGTERM, SIGKILL e waitpid, e as
execuções terminam sem processos ou objetos IPC residuais. O próximo passo
seria trocar o Turtlesim por Gazebo ou robôs reais, mantendo o plano POSIX.”

## Plano de contingência

- Se a janela não abrir, usar o slide 5 animado.
- Se o GIF não animar no visualizador, abrir os MP4 em `docs/media/`.
- Manter o PDF aberto como versão estática de segurança.
- Não iniciar instalação, build limpo ou edição de código durante a apresentação.
