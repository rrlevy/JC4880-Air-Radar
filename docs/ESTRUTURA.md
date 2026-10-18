# Estrutura do código

O firmware é um projeto ESP-IDF. `CMakeLists.txt` na raiz só declara o projeto. Tudo que é da aplicação está em `main/`, separado por assunto. O suporte da placa está em `components/board_p4`. LVGL, JPEG e o Wi-Fi remoto entram por `main/idf_component.yml` e são baixados para `managed_components/` na compilação. Essa pasta não vai para o Git.

Cada `.c` inclui o `.h` do próprio módulo. Dentro de `main/ui`, os arquivos também incluem `ui_internal.h`, que não é API pública.

## Fluxo

`app/main.c` sobe nesta ordem: NVS, `cfg_init`, `model_init`, display, brilho, toque, `wifi_app_start`, `ui_start`, `net_start`.

A tarefa de rede busca aviões e combina o resultado com o giro do radar. A tarefa da interface desenha e lê o toque. As duas não compartilham mutex: quem muda o centro ou o alcance avisa por um contador (`cfg_gen`, `cfg_range_gen`), e a rede descarta a resposta velha.

## `main/app`

| Arquivo | Função |
| --- | --- |
| `main.c` | `app_main`. Não apaga a NVS num boot normal. Só recria a partição se ela estiver corrompida ou numa versão que o IDF não lê. |

## `main/cfg`

Configuração do usuário. Namespace NVS `cfg`.

| Chave | Conteúdo |
| --- | --- |
| `lat`, `lon` | centro, em graus × 10⁷ |
| `rng` | índice do alcance |
| `flt` | máscara de filtro |
| `per` | período da varredura, em segundos |
| `hdg` | proa, 0 a 359 |
| `thm` | tema. Ausente ou inválido = Gol |
| `map`, `rio` | mapa e rios. Ausente = ligado |
| `ttl` | título ao lado da marca. Vazio volta para "Radar" |
| `knd` | tipo no radar. Bit `0x01` avião, `0x02` helicóptero. Ausente = só avião |

Alcances, do índice 0 ao 8: 1, 2, 5, 10, 15, 30, 50, 100 e 250 NM. O padrão de fábrica do índice é 15 NM. A placa pode ter outro valor já gravado. O índice 9 antigo era 500 NM e, na leitura, vira 250.

Bits de `flt`, nesta ordem, sem reaproveitar número: Gol `0x01`, LATAM `0x02`, Azul `0x04`, outros `0x08`. O bit `0x10` era o grupo Abra e não volta. Na leitura, esse bit é descartado; se a máscara ficar vazia, voltam todos os filtros.

`cfg.c` grava o conjunto quando algum campo muda. Não chama o modelo enquanto segura o próprio mutex.

## `main/model`

Aviões em voo, até 128. A lista nova entra em estágio e só substitui a da tela quando a linha da varredura passa no alto. Apagar a tela não zera a lista na hora: isso acontece depois de dois giros.

`model_blip_color` define a cor do ponto pela companhia. Essa cor não muda com o tema.

`radar_sync_*` é o relógio comum da varredura e da consulta. O ângulo 0 é para cima.

## `main/net`

HTTPS com o pacote de certificados do IDF.

| Pedido | Origem |
| --- | --- |
| aviões em volta do centro | `opendata.adsb.fi` |
| rota do voo | `api.adsbdb.com` |
| foto | `api.planespotters.net` |
| hora | NTP |

A API de ADS-B recusa distância acima de 250 NM com HTTP 400. O alcance máximo da tela é 250 NM, e o pedido sai com esse valor. Helicóptero (categoria A7, ou tipo de rotor) entra na lista só com o bit de `knd`. No mapa o ícone é o disco do rotor, com nariz curto no sentido do voo e cauda mais longa. Com a tela apagada, `net_set_dormant` corta as consultas; o Wi-Fi e o relógio seguem.

## `main/wifi`

O P4 não tem rádio. O C6 entra por SDIO (`esp_hosted`), nos pinos do `sdkconfig.defaults`. A senha fica no namespace NVS `wifi` (`ssid`, `pass` e a lista `known`). Não há senha no fonte.

## `main/ui`

LVGL 9, Montserrat, RGB565. A tela lógica é 800×480. O painel físico é 480×800; a rotação é do PPA, 270°, e o toque converte com `lx = y nativo`, `ly = 479 - x nativo`.

| Arquivo | O que é dele |
| --- | --- |
| `ui.h` | `ui_start` e os comandos da serial |
| `ui_internal.h` | estado, medidas e protótipos. Só os `.c` desta pasta incluem |
| `ui.c` | tema, relógio, varredura, brilho e a criação das telas |
| `ui_radar.c` | anel, cruz, mapa, rios, aeroportos, aviões, cardinais, zoom −/+ |
| `ui_card.c` | cartão da aeronave, rota e foto |
| `ui_input.c` | toque, pinça e gesto |
| `ui_menu.c` | Wi-Fi, Coordenadas, Filtros, Geral, Tema |
| `ui_shot.c` | cópia da tela e leitura dos comandos de texto |

O estado dos widgets mora em `ui.c` e é visto pelos outros arquivos pelos `extern` de `ui_internal.h`.

Medidas do radar: painel em (8, 56), 430×380; raio 154; centro 36 px abaixo do topo do painel, para N e S caberem fora do anel. As letras ficam a `raio + 22`. O texto do alcance fica dentro do anel.

Temas, nesta ordem: Gol `#FF7020`, LATAM `#E81848`, Azul `#0057B8`, Genérico `#39FF14`. Anel e cruz são o mesmo matiz, mais escuros. Gol e Genérico usam tinta escura no botão. LATAM e Azul usam tinta branca.

Rios: cor fixa `#3A4550`, espessura 1, desenhados por cima do contorno e por baixo do anel. Não escurecer de novo.

`tela` na serial só muda o backlight (aceso em duty 230, apagado em 0). Não chama `board_p4_backlight`, que forçaria o duty 255.

## `main/geo`

| Arquivo | Dado |
| --- | --- |
| `airports.c` | IATA brasileiro com serviço regular. Graus em `float`. Classe 1 grande, 2 médio, 3 local (OurAirports) e pista mais longa em metros |
| `brazil.c` | contorno dos estados, anéis fechados, graus × 100 |
| `rivers.c` | rios, polilinhas abertas, graus × 400, pares (lat, lon) |

Não fechar o traço dos rios. Não editar as tabelas à mão: são geradas. O Amazonas está nos dados, mas fica fora do anel mesmo em 250 NM quando o centro é São Paulo.

O anel desenha no máximo 12 aeroportos. Em 250 NM entram as classes 1 e 2. Abaixo disso entram as três. Se dois símbolos ficam a menos de 28 px, fica um: o mais próximo do centro entra primeiro e, entre os outros, a pista mais longa desempata. A separação é em pixels da tela, então vale com qualquer proa e qualquer centro.

## `main/tz`

`tz_grid.bin` é uma grade de 0,5°. Cada célula aponta uma string POSIX em `tz_rule.c`, que aplica o horário de verão. A tabela de zonas é alocada na PSRAM. O rótulo na tela é `UTC-3` ou `UTC+5:30`, sem o sufixo da cidade.

## `main/gfx`

`logos.c` monta imagens LVGL a partir dos blobs RGB565A8. O cabeçalho de cada arquivo tem largura, altura e stride em `uint16` little-endian, depois o plano de cor e o plano de alfa. O símbolo do linker usa o nome do arquivo (`_binary_gol_bin_start`), como o CMake lista em `EMBED_FILES`.

`jpgd/` decodifica o JPEG da foto. `jpgd_rgb565.cpp` reduz para caber no cartão e entrega RGB565.

## `components/board_p4`

BSP da placa, em C, para o painel ST7701S por MIPI-DSI e o toque GT911. Os pinos estão em `board_p4_pins.h`. O framebuffer lógico que a interface copia é o de antes da rotação apresentada no vidro.

Pinos que o resto do firmware não deve reutilizar: LCD reset 5, backlight 23, toque 7 e 8 (reset 3), SD 39–44, SDIO do C6 14–19, reset do C6 54, I2S 9/10/12/13/48, amplificador 11, BOOT 35, LED 26 (compartilhado com o TX do RS-485), UART0 37/38, RX do RS-485 27.

## `telas/`

PNG da interface, só para documentação. Não entram no binário. Esta cópia não traz as capturas anteriores. Quando a tela mudar, as novas capturas entram aqui.

## O que não mudar sem querer

- Não renumerar filtro, tema ou as chaves da NVS.
- Não girar a tela de novo e não remapear o toque.
- Não gravar o C6 nem apagar a NVS para atualizar o aplicativo.
- Não pôr a cor dos rios no tema, nem a cor dos aviões.
- Não ampliar as marcas da tela de temas acima da escala 256.
- Não chamar `model_*` com o mutex de `cfg` tomado.
