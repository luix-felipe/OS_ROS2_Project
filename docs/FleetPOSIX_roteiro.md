# Roteiro — apresentação FleetPOSIX

Duração estimada: **6 a 7 minutos**.

Apresentação oficial:
[FleetPOSIX no Canva](https://www.canva.com/design/DAHb4r2qxhU/bvRyO4jD2x-MbVPsWD2EyQ/edit).
Este roteiro é organizado por temas. Confira a ordem e a numeração dos
slides na versão atual do Canva ao ensaiar.

Antes de apresentar, executar `make verify` e `make simulate` na raiz do
repositorio, no Ubuntu/WSL. A referencia de entrega de 25/11/2026 aprova os
13 cenarios (12 Python + um C++). Os logs e as capturas de 23/11/2026 ficam em
`docs/evidence/2026-11-23/`; a validacao atual esta em
`docs/evidence/2026-11-25/README.md`.

## Motivação (50 s)

“Nosso problema é coordenar três robôs independentes que compartilham um
corredor de fábrica. O operador envia missões durante a execução. O Turtlesim
serve como representação visual dos AMRs, mas todo o plano de controle foi
implementado diretamente com mecanismos POSIX.”

## Arquitetura (65 s)

“O supervisor cria o Turtlesim e três processos agentes com `fork` e `exec`.
Cada agente possui três pthreads: uma para o executor ROS, uma produtora que
recebe comandos e uma consumidora que controla o movimento. O supervisor
recolhe todos os processos usando `waitpid`.”

## Conceitos da disciplina (70 s)

Apontar rapidamente uma linha de cada vez. Destacar:

- mutex robusto e compartilhado entre processos;
- semáforo do corredor e semáforos do buffer;
- pipe para logs e FIFO para comandos externos;
- fila POSIX por AMR;
- memória compartilhada para o estado global.

## Seção crítica (55 s)

“As três rotas se cruzam somente dentro da região crítica. No modo seguro, o
AMR para antes da região, executa `sem_clockwait`, atravessa e libera com
`sem_post`. No modo inseguro, essa aquisição é desativada.”

## Resultados das simulações (90 s)

Use as demonstrações ao vivo ou os vídeos e GIFs em `docs/media/` como apoio.

“À esquerda, apenas um AMR atravessa enquanto os outros aguardam: três missões
e zero colisões. À direita, sem o semáforo, os três convergem. O supervisor
detectou duas colisões lógicas pela distância entre as poses.”

Se houver tempo, executar ao vivo:

```bash
make demo
make demo-unsafe
```

## Validação e conclusão (60 s)

“Além da demonstração, temos doze cenários Python e um teste C++ do mutex
robusto. O shutdown possui contingência com SIGTERM, SIGKILL e waitpid, e as
execuções terminam sem processos ou objetos IPC residuais. Os alvos Make
carregam o ambiente e permitem repetir a comparação. O Turtlesim é uma
abstração visual 2D; o resultado de zero colisões se refere às rotas de
referência, sem planejamento ou desvio de obstáculos.”

Mostre `make help`, explique `make verify`, `make simulate`, `make demo`
e `make run`; `make ipc` inspeciona os processos, a memória, as filas e o FIFO.

## Exemplo online para a demonstração ao vivo

Abra `make run`, aguarde os três agentes online e envie:

```text
enviar 1 9.0 5.15
enviar 2 5.5 9.0
enviar 3 2.0 5.85
```

Durante o movimento, execute `pausar 1` e `status`. Explique que um AMR
pausado dentro do corredor conserva sua reserva. Depois use `retomar 1` e
`status`. Encerre com `sair` ou `Ctrl+C` e confira `make ipc`.

Para uma comparação online insegura, inicie `make run-unsafe` em uma nova
execução e envie as mesmas três missões. A contagem depende do momento de
envio; as demos automáticas são o roteiro para comparar os resultados.

## Plano de contingência

- Se a janela não abrir, usar os vídeos das simulações em `docs/media/`.
- Conferir o acesso à apresentação oficial no Canva antes de apresentar.
- Exportar a versão atual do Canva para apresentação sem internet, quando necessário.
- Não iniciar instalação, build limpo ou edição de código durante a apresentação.
