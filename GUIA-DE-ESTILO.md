# Guia de estilo de C

Como o código C deste repositório é escrito, e por quê. Vale para os módulos
daqui e para os projetos criados a partir dos templates `tpl-*`. As regras são
as de C embarcado no estilo do kernel Linux e do Zephyr, que é o mais comum em
firmware profissional.

Regra geral: na dúvida, o código novo imita o código que já está ao lado.

## 1. Estrutura de um módulo

- Um módulo é um par `src/<modulo>.c` e `src/<modulo>.h`, com o teste em
  `tests/test_<modulo>.c` e a página em `docs/<modulo>.md`.
- O `.h` é a interface pública. O que é só do módulo fica `static` no `.c`.
- Cada módulo se basta: copiar o par `.c`/`.h` para outro projeto tem que
  funcionar, sem arrastar outro arquivo.
- Todo arquivo abre com um comentário de bloco: uma linha com nome e propósito,
  depois o que precisa ser dito sobre comportamento, limites e concorrência.
- Guarda de inclusão no formato `#ifndef <MODULO>_H`, fechada com
  `#endif /* <MODULO>_H */`. Nada de nome começando com sublinhado, que é
  reservado pela linguagem.
- Header público leva o bloco `extern "C"`, para poder ser incluído de C++.
- Ordem dos includes: o header do próprio módulo primeiro, depois os outros do
  projeto, depois os do sistema. O próprio primeiro prova que ele compila
  sozinho.

## 2. Nomes

| O quê | Formato | Exemplo |
| --- | --- | --- |
| Arquivo | `snake_case` | `ringbuf.c` |
| Função pública | `<modulo>_<verbo>` ou `<modulo>_<objeto>_<verbo>` | `ringbuf_put`, `frame_parser_feed` |
| Função privada (`static`) | nome curto, sem prefixo | `next_index`, `debounce` |
| Tipo | `typedef struct <nome> { ... } <nome>_t;` (com tag e com `_t`) | `ringbuf_t` |
| Constante de enum | `<MODULO>_<NOME>` em maiúsculas | `BUTTON_EVENT_CLICK` |
| Macro e constante | `<MODULO>_<NOME>` em maiúsculas | `FRAME_MAX_PAYLOAD` |
| Callback | termina em `_cb`, `_handler` ou `_fn` | `on_byte_cb` |

- O prefixo do módulo é o nome do arquivo. Quem lê `button_update` sabe onde
  procurar.
- Verbos usuais: `init`, `get`, `set`, `put`, `read`, `write`, `update`,
  `reset`, `is_<condição>`.
- Unidade vai no nome: `timeout_ms`, `debounce_ticks`, `FRAME_LEN`,
  `battery_mv`. Um número sem unidade é um bug esperando para acontecer.
- Nada de prefixo `g_`, `s_` ou `m_` em variável. Estado de módulo é `static`
  de arquivo com nome comum.

## 3. Erros

- Função que pode falhar por mau uso (ponteiro nulo, tamanho inválido) devolve
  `int`: `0` é sucesso, valor negativo de `<errno.h>` é o erro (`-EINVAL`,
  `-ENOSPC`, `-ENODATA`).
- Pergunta de sim ou não devolve `bool`: `button_is_pressed`, "coube na fila",
  "tinha byte para ler".
- Resultado com vários desfechos válidos devolve um `enum`, não um código de
  erro: `FRAME_READY`, `BUTTON_EVENT_CLICK`.
- Dado ruim que chega de fora (ruído na serial, CRC errado) não é erro de
  programação: é um desfecho normal, informado pelo `enum` ou por um campo.
- Parâmetro de saída fica no fim da lista (`*out`, `*length`). Se a função
  falha, a saída não é tocada, e o header diz isso.
- Valida os argumentos no topo e retorna cedo. Sem `goto`, salvo um único
  rótulo de limpeza quando há recurso para liberar.
- Retorno ignorado de propósito leva `(void)` na frente.
- Macro que avalia o argumento duas vezes é proibida. Se precisa de macro com
  argumento, cada argumento aparece uma vez só.

## 4. Tipos e constantes

- Dados usam os tipos de largura fixa de `<stdint.h>`. Tamanho e índice usam
  `size_t`. Verdadeiro e falso usam `bool`.
- Literal sem sinal leva `U` (`0x8000U`, `8U`). Misturar com sinal e sem sinal
  em conta ou comparação é fonte clássica de bug.
- Conversão que estreita o tipo leva cast explícito. O compilador exige
  (`-Wconversion`), e o cast mostra que foi de propósito.
- `const` em tudo que não muda: ponteiro de entrada (`const uint8_t *data`),
  tabela (`static const`), variável local calculada uma vez.
- `enum` para estados e desfechos. `#define` para tamanhos e valores numéricos.
- Sem ponto flutuante. Valor fracionário vira inteiro com a escala no nome
  (`temperature_x100`).
- Invariante de tamanho ou de configuração é conferida na compilação com
  `_Static_assert`.

## 5. Estado e memória

- Sem alocação dinâmica (`malloc`). Quem chama é dono da memória e passa o
  buffer ou a struct.
- O estado de uma instância fica numa struct de quem chama, passada como
  primeiro parâmetro (`ringbuf_t *ring`). Assim cabem várias instâncias e o
  módulo é reentrante.
- Todo módulo com estado tem `<modulo>_init`.
- Máquina de estados: um `enum` de estados e um `switch`, com `default` que
  volta ao estado inicial. É mais fácil de ler e de depurar do que tabela.
- Formato de fio (bytes na serial, no rádio) é montado e lido byte a byte, com
  a ordem dos bytes escrita no código. Nada de struct empacotada nem de
  bitfield para isso: o layout deles depende do compilador.
- O que vale para concorrência fica escrito no header: quem pode chamar o quê,
  de interrupção ou não.

## 6. Camadas

- Lógica em C puro, sem header de SDK, de RTOS ou de fabricante. É isso que
  permite testar no PC.
- Hardware fica numa camada fina por cima (o `main.c` dos templates), que só
  lê pino, relógio e fila e chama a lógica.
- A lógica não lê relógio nem pino: recebe o tempo e o nível como parâmetro.
- Quando a lógica precisa agir no mundo, recebe ponteiros de função (uma struct
  `<modulo>_ops` `const`) no `init`.
- A dependência só desce: aplicação usa módulo, módulo não conhece aplicação.

## 7. Formatação

- Quem formata é o `clang-format`, com o `.clang-format` da raiz: `make format`.
- Tab de largura 8 para indentar, linha de até 110 colunas.
- Chave da função na linha de baixo; chave de `if`, `for`, `while` e `switch`
  na mesma linha.
- Sempre com chaves, mesmo em bloco de uma linha.
- Comentários em inglês, dizendo o porquê. O quê o código já diz.

## 8. Testes e análise

- Todo módulo tem teste de host, compilado com AddressSanitizer e
  UndefinedBehaviorSanitizer.
- O teste cobre: argumento inválido, os limites exatos (um a menos, exato, um a
  mais), o caminho normal e entrada aleatória com semente fixa.
- Quando existe um valor publicado ou um modelo de referência simples, o teste
  compara com ele.
- O que o teste de host não prova (concorrência real, tempo real, hardware)
  fica escrito na página do módulo.
- Avisos são erros. A lista de avisos está no `Makefile`.
- `cppcheck` roda no CI. Supressão só na linha, com o motivo escrito.

## 9. Antes de abrir um PR

```bash
make format
```

```bash
make test proof analyze
```

Commits em Conventional Commits, em inglês, com assunto de até 72 caracteres.
