# STM32 Nucleo L476RG: port futuro (issue #3)

Esta pasta é só um ponto de partida para o port do cluster para a STM32 Nucleo L476RG.
O `src/main.c` atual é um esqueleto: um super-loop que chama stubs das tarefas, sem
Trampoline, sem configuração de clock e sem UART. Nada deste diretório foi usado nas
medições do projeto, que rodam no alvo POSIX (ver `docs/relatorio_tecnico.md`).

Para o port, a seção 12 do relatório lista o que falta: startup, linker script, relógio,
SysTick como `SystemCounter` (1 ms) e CAN via bxCAN ou MCP2515 + TJA1050.
